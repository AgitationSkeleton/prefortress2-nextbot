// pf2-nextbot: keep an empty PF2 server awake so TFBots can fill it (TF2: tf_allow_server_hibernation 0).
//
// PF2's engine (Source SDK 2013 previous2021 branch) hibernates when no human is connected: it stops
// running game frames and punts fake clients ("Punting bot, server is hibernating"), and there is
// no cvar for it. CGameServer::UpdateHibernationState starts with `bool bHaveAnyClients = false`
// (xor bh,bh). We flip that initialiser to `true` (mov bh,1) in memory only, after verifying the
// byte pattern occurs exactly once in engine.dll, and restore the original bytes on unload.
// This TU includes only <windows.h> to avoid SDK macro collisions.
//
// Linux: not implemented. The byte pattern above is specific to the Windows engine.dll and has not been
// verified against a Linux engine.so, so on Linux this is a logged no-op (see the #else branch).
#if defined(_WIN32)
#include <windows.h>
#include <string.h>
#include <stdio.h>

namespace pf2 {

static const unsigned char kSig[] = { 0x53, 0x56, 0x32, 0xFF, 0x33, 0xF6, 0x88, 0x7D, 0xFF };
static const int kPatchOff = 2;
static const unsigned char kOrig[2] = { 0x32, 0xFF };    // xor bh,bh
static const unsigned char kPatch[2] = { 0xB7, 0x01 };   // mov bh,1
static unsigned char *s_site = NULL;
static bool s_searched = false;

static unsigned char *FindUniqueSite()
{
	HMODULE h = GetModuleHandleA("engine.dll");
	if (!h)
		return NULL;
	unsigned char *base = (unsigned char *)h;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return NULL;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return NULL;
	IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
	unsigned char *found = NULL;
	int count = 0;
	for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec)
	{
		if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE))
			continue;
		unsigned char *p = base + sec->VirtualAddress;
		size_t n = sec->Misc.VirtualSize;
		for (size_t i = 0; i + sizeof(kSig) <= n; ++i)
		{
			if (p[i] != kSig[0])
				continue;
			// accept either the original or our patched bytes at the patch offset
			if (memcmp(p + i, kSig, kPatchOff) == 0 && memcmp(p + i + kPatchOff + 2, kSig + kPatchOff + 2, sizeof(kSig) - kPatchOff - 2) == 0 &&
				(memcmp(p + i + kPatchOff, kOrig, 2) == 0 || memcmp(p + i + kPatchOff, kPatch, 2) == 0))
			{
				found = p + i + kPatchOff;
				++count;
			}
		}
	}
	return count == 1 ? found : NULL;
}

static bool WriteBytes(unsigned char *at, const unsigned char *bytes)
{
	DWORD old = 0;
	if (!VirtualProtect(at, 2, PAGE_EXECUTE_READWRITE, &old))
		return false;
	memcpy(at, bytes, 2);
	DWORD tmp = 0;
	VirtualProtect(at, 2, old, &tmp);
	FlushInstructionCache(GetCurrentProcess(), at, 2);
	return true;
}

// returns a status string for the log
const char *Hibernation_Set(bool preventHibernation)
{
	static char status[160];
	if (!s_searched)
	{
		s_site = FindUniqueSite();
		s_searched = true;
	}
	if (!s_site)
		return "hibernation: engine.dll pattern not found exactly once; leaving the engine unchanged";
	const unsigned char *want = preventHibernation ? kPatch : kOrig;
	if (memcmp(s_site, want, 2) == 0)
		return preventHibernation ? "hibernation: already prevented" : "hibernation: engine default";
	if (!WriteBytes(s_site, want))
		return "hibernation: VirtualProtect failed; leaving the engine unchanged";
	_snprintf(status, sizeof(status), "hibernation: %s", preventHibernation ? "prevented (server stays awake when empty)" : "restored engine default");
	return status;
}

} // namespace pf2

#else // !_WIN32

namespace pf2 {

// returns a status string for the log
const char *Hibernation_Set(bool preventHibernation)
{
	(void)preventHibernation;
	return "hibernation: hibernation control unsupported on Linux in this build (engine left unchanged)";
}

} // namespace pf2

#endif // _WIN32
