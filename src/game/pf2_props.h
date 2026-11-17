// pf2-nextbot: look up entity fields by name at runtime (sendprops and datamaps).
// PF2's shipped server.dll is newer than its public source, so nothing is hardcoded.
#pragma once
#include <stddef.h>

struct datamap_t;
class ServerClass;

namespace pf2 {

// Offset of a networked property, searched through the entity's own ServerClass tables
// (base tables included). Returns -1 if not found. "Name" may be "m_Shared.m_nPlayerCond" style
// path is not needed: nested tables are searched depth first and offsets are summed.
int SendPropOffset(const void *rawEntity, const char *prop);
int SendPropOffsetByClass(const char *serverClassName, const char *prop);

// Offset of a datamap field (walks base maps and embedded structs). -1 if not found.
int DataMapOffset(const void *rawEntity, const char *field);

// Offset by either source: datamap first (covers non-networked fields), then sendprop.
int FieldOffset(const void *rawEntity, const char *field);

void ResetPropCache();

template <typename T> inline T &FieldRef(const void *raw, int offset) { return *(T *)((char *)raw + offset); }

// Cached per call site: PF2_FIELD(type, raw, "m_iHealth") -> T* or NULL.
struct PropSlot { const void *dmap; int offset; };
int CachedFieldOffset(PropSlot &slot, const void *raw, const char *name, bool sendOnly);

} // namespace pf2

#define PF2_FIELD_PTR(type, raw, name) \
	([&]() -> type * { static pf2::PropSlot _s = { 0, -2 }; \
		int _o = pf2::CachedFieldOffset(_s, (raw), (name), false); \
		return _o < 0 ? (type *)0 : (type *)((char *)(raw) + _o); }())

#define PF2_SENDPROP_PTR(type, raw, name) \
	([&]() -> type * { static pf2::PropSlot _s = { 0, -2 }; \
		int _o = pf2::CachedFieldOffset(_s, (raw), (name), true); \
		return _o < 0 ? (type *)0 : (type *)((char *)(raw) + _o); }())
