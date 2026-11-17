// pf2-nextbot: call a virtual function on a real game object by vtable slot.
// Slots come from tools/vtdump.py and are listed (per platform) in game/pf2_offsets.h.
//
// Windows (MSVC x86): virtual member functions use __thiscall (this in ecx), so the call goes through
// a member-function pointer of a trivial single-inheritance class (4 bytes, a plain code address).
// Linux (GCC/Clang i386, Itanium ABI): member functions are cdecl with `this` as the first stack
// argument (a hidden struct-return pointer, if any, comes before it, exactly as for a free function
// returning the same type), so a plain function pointer taking `this` first is ABI-identical.
#pragma once

namespace pf2 {

#if defined(_MSC_VER)

class VEmpty {};   // complete, single-inheritance: member-function pointers are 4 bytes

template <typename R, typename... A>
inline R VCall(const void *thisptr, int index, A... args)
{
	typedef R (VEmpty::*Fn)(A...);
	static_assert(sizeof(Fn) == sizeof(void *), "unexpected member-function-pointer size");
	void **vt = *(void ***)thisptr;
	union { void *addr; Fn fn; } u;
	u.addr = vt[index];
	return (((VEmpty *)thisptr)->*u.fn)(args...);
}

#else

template <typename R, typename... A>
inline R VCall(const void *thisptr, int index, A... args)
{
	typedef R (*Fn)(void *, A...);
	void **vt = *(void ***)thisptr;
	Fn fn = (Fn)vt[index];
	return fn(const_cast<void *>(thisptr), args...);
}

#endif

} // namespace pf2
