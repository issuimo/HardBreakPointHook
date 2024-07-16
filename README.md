# HardBreakPointHook

``` C++
HardBreakPoint::SetBreakPoint(HitRole, HitRole_Hook);

static bool InitPoint_Hook(BulletControl* ptr) {
    ptr->maxDistance = 9999.0f;
    return HardBreakPoint::CallOrigin(InitPoint_Hook, ptr);
}
```
