# 硬件断点Hook
# HardBreakPointHook
> 最多四处断点\
> Up to four breakpoints
#### 使用方法
``` C++
HardBreakPoint::Initialize();

HardBreakPoint::SetBreakPoint(HitRole, HitRole_Hook);

static bool InitPoint_Hook(BulletControl* ptr) {
    ptr->maxDistance = 9999.0f;
    return HardBreakPoint::CallOrigin(InitPoint_Hook, ptr);
}
```
