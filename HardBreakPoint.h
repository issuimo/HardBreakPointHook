#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include "phmap.h"
#include <vector>
class HardBreakPoint {
public:
	struct BreakPoint {
		int id;
		void* original;
		void* replacement;
	};

	struct DR7 {
		unsigned int L0 : 1;     // 局部断点0
		unsigned int G0 : 1;     // 全局断点0
		unsigned int L1 : 1;     // 局部断点1
		unsigned int G1 : 1;     // 全局断点1
		unsigned int L2 : 1;     // 局部断点2
		unsigned int G2 : 1;     // 全局断点2
		unsigned int L3 : 1;     // 局部断点3
		unsigned int G3 : 1;     // 全局断点3
		unsigned int LE : 1;     // 局部精确断点
		unsigned int GE : 1;     // 全局精确断点
		unsigned int reserved1 : 1; // 保留位
		unsigned int RTM : 1;    // 受限事务内存
		unsigned int reserved2 : 1; // 保留位
		unsigned int GD : 1;     // 常规检测
		unsigned int reserved3 : 2; // 保留位
		unsigned int RW0 : 2;    // 断点条件0
		unsigned int LEN0 : 2;   // 断点长度0
		unsigned int RW1 : 2;    // 断点条件1
		unsigned int LEN1 : 2;   // 断点长度1
		unsigned int RW2 : 2;    // 断点条件2
		unsigned int LEN2 : 2;   // 断点长度2
		unsigned int RW3 : 2;    // 断点条件3
		unsigned int LEN3 : 2;   // 断点长度3

		static uint32_t DR7ToDWORD(const DR7& dr7) {
			uint32_t value = 0;
			value |= dr7.L0 << 0;
			value |= dr7.G0 << 1;
			value |= dr7.L1 << 2;
			value |= dr7.G1 << 3;
			value |= dr7.L2 << 4;
			value |= dr7.G2 << 5;
			value |= dr7.L3 << 6;
			value |= dr7.G3 << 7;
			value |= dr7.LE << 8;
			value |= dr7.GE << 9;
			value |= dr7.reserved1 << 10;
			value |= dr7.RTM << 11;
			value |= dr7.reserved2 << 12;
			value |= dr7.GD << 13;
			value |= dr7.reserved3 << 14;
			value |= dr7.RW0 << 16;
			value |= dr7.LEN0 << 18;
			value |= dr7.RW1 << 20;
			value |= dr7.LEN1 << 22;
			value |= dr7.RW2 << 24;
			value |= dr7.LEN2 << 26;
			value |= dr7.RW3 << 28;
			value |= dr7.LEN3 << 30;
			return value;
		}
	};

	HardBreakPoint() = delete;

	static void Initialize() {
		AddVectoredExceptionHandler(999999, VectoredExceptionHandler);
	}

	template<typename R, typename... Args>
	static bool SetBreakPoint(R(*address)(Args...), R(*replacement)(Args...)) {
		LOG_TRACE("挂钩函数: %p %p", address, replacement);
		BreakPoint bp;
		bp.original = address;
		bp.replacement = replacement;
		for (int i = 0; i < 4; i++) {
			if (!bp_status[i]) {
				bp.id = i;
				breakpoints.insert({ address, bp });
				bp_status[i] = true;

				DWORD myId = GetCurrentThreadId();
				auto threads = GetProcessThreads(GetCurrentProcessId());

				for (auto thread : threads) {
					if (thread != myId) {
						HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, thread);
						if (hThread == nullptr) {
							continue;
						}
						SuspendThread(hThread);

						if (breakpoints.size() < 4) {


							CONTEXT ctx = { 0 };
							ctx.ContextFlags = CONTEXT_ALL;
							if (!GetThreadContext(hThread, &ctx)) {
								ResumeThread(hThread);
								CloseHandle(hThread);
								continue;
							}


							switch (i) {
							case 0:
								ctx.Dr0 = reinterpret_cast<DWORD_PTR>(address);
								dr7.L0 = 1;
								dr7.RW0 = 0;
								break;
							case 1:
								ctx.Dr1 = reinterpret_cast<DWORD_PTR>(address);
								dr7.L1 = 1;
								dr7.RW1 = 0;
								break;
							case 2:
								ctx.Dr2 = reinterpret_cast<DWORD_PTR>(address);
								dr7.L2 = 1;
								dr7.RW2 = 0;
								break;
							case 3:
								ctx.Dr3 = reinterpret_cast<DWORD_PTR>(address);
								dr7.L3 = 1;
								dr7.RW3 = 0;
								break;
							}
							ctx.Dr7 = DR7::DR7ToDWORD(dr7);
							ctx.ContextFlags = CONTEXT_ALL;
							SetThreadContext(hThread, &ctx);
						}

						ResumeThread(hThread);
						CloseHandle(hThread);
					}
				}
				break;
			}
		}
		return true;
	}

	template<typename R, typename... Args>
	static bool RemoveBreakPoint(R(*address)(Args...)) {
		auto it = breakpoints.find(address);
		if (it == breakpoints.end()) {
			return false;
		}

		BreakPoint bp = it->second;
		DWORD myId = GetCurrentThreadId();
		auto threads = GetProcessThreads(GetCurrentProcessId());

		for (auto thread : threads) {
			if (thread != myId) {
				HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, thread);
				if (hThread == nullptr) {
					continue;
				}
				SuspendThread(hThread);

				CONTEXT ctx = { 0 };
				ctx.ContextFlags = CONTEXT_ALL;
				if (GetThreadContext(hThread, &ctx)) {
					switch (bp.id) {
						case 0: ctx.Dr0 = 0; break;
						case 1: ctx.Dr1 = 0; break;
						case 2: ctx.Dr2 = 0; break;
						case 3: ctx.Dr3 = 0; break;
					}
					ctx.Dr7 = DR7::DR7ToDWORD(dr7);
					ctx.ContextFlags = CONTEXT_ALL;
					SetThreadContext(hThread, &ctx);
				}

				ResumeThread(hThread);
				CloseHandle(hThread);
			}
		}

		bp_status[bp.id] = false;
		breakpoints.erase(it);
		return true;
	}

	template<typename R, typename... Args>
	static R CallOrigin(R(*func)(Args...), Args... args) {
		for (auto [fst, snd] : breakpoints) {
			if (snd.replacement == func) {
				if constexpr (std::is_void_v<R>) {
					RemoveBreakPointThread(GetCurrentThread(), reinterpret_cast<R(*)(Args...)>(snd.original));
					reinterpret_cast<R(*)(Args...)>(snd.original)(args...);
					SetBreakPointThread(GetCurrentThread(), reinterpret_cast<R(*)(Args...)>(snd.original), reinterpret_cast<R(*)(Args...)>(snd.replacement));
					return;
				} else {
					RemoveBreakPointThread(GetCurrentThread(), reinterpret_cast<R(*)(Args...)>(snd.original));
					R ret = reinterpret_cast<R(*)(Args...)>(snd.original)(args...);
					SetBreakPointThread(GetCurrentThread(), reinterpret_cast<R(*)(Args...)>(snd.original), reinterpret_cast<R(*)(Args...)>(snd.replacement));
					return ret;
				}
			}
		}
		return R();
	}

	inline static DR7 dr7;
private:
	inline static bool bp_status[] = {false, false, false, false};
	inline static inline phmap::parallel_flat_hash_map<void*, BreakPoint, phmap::priv::hash_default_hash<void*>, phmap::priv::hash_default_eq<void*>, phmap::priv::Allocator<std::pair<void*, BreakPoint>>, 4, std::mutex> breakpoints;


	template<typename R, typename... Args>
	static bool RemoveBreakPointThread(HANDLE hThread, R(*address)(Args...)) {
		auto it = breakpoints.find(address);
		if (it == breakpoints.end()) {
			return false;
		}

		BreakPoint bp = it->second;

		CONTEXT ctx = { 0 };
		ctx.ContextFlags = CONTEXT_ALL;
		if (GetThreadContext(hThread, &ctx)) {
			switch (bp.id) {
				case 0: ctx.Dr0 = 0; break;
				case 1: ctx.Dr1 = 0; break;
				case 2: ctx.Dr2 = 0; break;
				case 3: ctx.Dr3 = 0; break;
			}
			ctx.Dr7 = DR7::DR7ToDWORD(dr7);
			ctx.ContextFlags = CONTEXT_ALL;
			SetThreadContext(hThread, &ctx);
		}

		bp_status[bp.id] = false;
		breakpoints.erase(it);
		return true;
	}

	template<typename R, typename... Args>
	static bool SetBreakPointThread(HANDLE hThread, R(*address)(Args...), R(*replacement)(Args...)) {
		if (breakpoints.size() < 4) {
			BreakPoint bp;
			bp.original = address;
			bp.replacement = replacement;

			CONTEXT ctx = { 0 };
			ctx.ContextFlags = CONTEXT_ALL;
			GetThreadContext(hThread, &ctx);

			for (int i = 0; i < 4; i++) {
				if (!bp_status[i]) {
					bp.id = i;
					breakpoints.insert({ address, bp });
					bp_status[i] = true;

					switch (i) {
					case 0:
						ctx.Dr0 = reinterpret_cast<DWORD_PTR>(address);
						dr7.L0 = 1;
						dr7.RW0 = 0;
						break;
					case 1:
						ctx.Dr1 = reinterpret_cast<DWORD_PTR>(address);
						dr7.L1 = 1;
						dr7.RW1 = 0;
						break;
					case 2:
						ctx.Dr2 = reinterpret_cast<DWORD_PTR>(address);
						dr7.L2 = 1;
						dr7.RW2 = 0;
						break;
					case 3:
						ctx.Dr3 = reinterpret_cast<DWORD_PTR>(address);
						dr7.L3 = 1;
						dr7.RW3 = 0;
						break;
					}
					ctx.Dr7 = DR7::DR7ToDWORD(dr7);
					ctx.ContextFlags = CONTEXT_ALL;
					SetThreadContext(hThread, &ctx);
					break;
				}
			}
		}
		return false;
	}

	static std::vector<DWORD> GetProcessThreads(DWORD processID) {
		std::vector<DWORD> threadIDs;
		HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (hThreadSnap == INVALID_HANDLE_VALUE) {
			return threadIDs;
		}

		THREADENTRY32 te32;
		te32.dwSize = sizeof(THREADENTRY32);

		if (Thread32First(hThreadSnap, &te32)) {
			do {
				if (te32.th32OwnerProcessID == processID) {
					threadIDs.push_back(te32.th32ThreadID);
				}
			} while (Thread32Next(hThreadSnap, &te32));
		}

		CloseHandle(hThreadSnap);
		return threadIDs;
	}

	static auto WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS exception) -> LONG {
		if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
#ifdef _WIN64
			if (breakpoints.contains(exception->ExceptionRecord->ExceptionAddress)) {
				exception->ContextRecord->Dr7 = DR7::DR7ToDWORD(dr7);
				exception->ContextRecord->Rip = reinterpret_cast<DWORD64>(breakpoints[exception->ExceptionRecord->ExceptionAddress].replacement);
				return EXCEPTION_CONTINUE_EXECUTION;
			}
#else
			if (breakpoints.contains(exception->ExceptionRecord->ExceptionAddress)) {
				exception->ContextRecord->Dr7 = DR7::DR7ToDWORD(dr7);
				exception->ContextRecord->Eip = reinterpret_cast<DWORD>(breakpoints[exception->ExceptionRecord->ExceptionAddress].replacement);
				return EXCEPTION_CONTINUE_EXECUTION;
			}
#endif
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}
};