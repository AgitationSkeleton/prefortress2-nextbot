// pf2-nextbot: runtime field lookup (see pf2_props.h)
#include <string.h>
#include <stdio.h>
#include <tier0/platform.h>
#include <tier1/strtools.h>
#include <dt_send.h>
#include <server_class.h>
#include <datamap.h>
#include <eiface.h>
#include "pf2_props.h"
#include "pf2_offsets.h"
#include "vfunc.h"

extern IServerGameDLL *gamedll;

namespace pf2 {

static int SearchSendTable(SendTable *table, const char *prop, int base)
{
	for (int i = 0; i < table->GetNumProps(); ++i)
	{
		SendProp *p = table->GetProp(i);
		const char *n = p->GetName();
		if (n && !V_strcmp(n, prop))
			return base + p->GetOffset();
		if (p->GetType() == DPT_DataTable && p->GetDataTable())
		{
			int r = SearchSendTable(p->GetDataTable(), prop, base + p->GetOffset());
			if (r >= 0)
				return r;
		}
	}
	return -1;
}

// NBPERF: field lookups by name are on every hot path (team, health, flags ... for every entity a bot
// considers, every frame). Searching the datamap / sendtable by string each time dominated the bot
// CPU cost, so results are cached per (class table, field name hash). Class tables live in the game
// DLL's static data, so their addresses never change while it is loaded.
struct OffsetCacheEntry { const void *table; unsigned long long nameHash; int kind; int offset; };
static const int OFFSET_CACHE_SIZE = 8192;   // power of two
static OffsetCacheEntry s_offsetCache[OFFSET_CACHE_SIZE];

static unsigned long long HashName(const char *s)
{
	unsigned long long h = 1469598103934665603ULL;
	for (; *s; ++s)
		h = (h ^ (unsigned char)*s) * 1099511628211ULL;
	return h;
}

// returns true and sets *offset if cached; otherwise *slot is where to store the result
static bool OffsetCacheFind(const void *table, const char *name, int kind, int *offset, OffsetCacheEntry **slot)
{
	unsigned long long h = HashName(name);
	unsigned int i = (unsigned int)((h ^ (unsigned long long)(size_t)table * 0x9E3779B97F4A7C15ULL ^ (unsigned)kind) >> 17) & (OFFSET_CACHE_SIZE - 1);
	for (int probe = 0; probe < 16; ++probe, i = (i + 1) & (OFFSET_CACHE_SIZE - 1))
	{
		OffsetCacheEntry &e = s_offsetCache[i];
		if (!e.table)
		{
			e.nameHash = h;
			e.kind = kind;
			*slot = &e;
			return false;
		}
		if (e.table == table && e.nameHash == h && e.kind == kind)
		{
			*offset = e.offset;
			return true;
		}
	}
	*slot = NULL;   // neighbourhood full: don't cache
	return false;
}

int SendPropOffset(const void *raw, const char *prop)
{
	if (!raw)
		return -1;
	ServerClass *sc = VCall<ServerClass *>(raw, vt::GetServerClass);
	if (!sc || !sc->m_pTable)
		return -1;
	int off;
	OffsetCacheEntry *slot;
	if (OffsetCacheFind(sc, prop, 1, &off, &slot))
		return off;
	off = SearchSendTable(sc->m_pTable, prop, 0);
	if (slot)
	{
		slot->offset = off;
		slot->table = sc;
	}
	return off;
}

int SendPropOffsetByClass(const char *className, const char *prop)
{
	for (ServerClass *sc = gamedll->GetAllServerClasses(); sc; sc = sc->m_pNext)
		if (!V_strcmp(sc->GetName(), className))
			return SearchSendTable(sc->m_pTable, prop, 0);
	return -1;
}

static int SearchDataMap(datamap_t *map, const char *field, int base)
{
	for (; map; map = map->baseMap)
	{
		for (int i = 0; i < map->dataNumFields; ++i)
		{
			typedescription_t &td = map->dataDesc[i];
			if (td.fieldName && !V_strcmp(td.fieldName, field))
				return base + td.fieldOffset[TD_OFFSET_NORMAL];
			if (td.fieldType == FIELD_EMBEDDED && td.td)
			{
				int r = SearchDataMap(td.td, field, base + td.fieldOffset[TD_OFFSET_NORMAL]);
				if (r >= 0)
					return r;
			}
		}
	}
	return -1;
}

int DataMapOffset(const void *raw, const char *field)
{
	if (!raw)
		return -1;
	datamap_t *map = VCall<datamap_t *>(raw, vt::GetDataDescMap);
	if (!map)
		return -1;
	int off;
	OffsetCacheEntry *slot;
	if (OffsetCacheFind(map, field, 2, &off, &slot))
		return off;
	off = SearchDataMap(map, field, 0);
	if (slot)
	{
		slot->offset = off;
		slot->table = map;
	}
	return off;
}

int FieldOffset(const void *raw, const char *field)
{
	int o = DataMapOffset(raw, field);
	return o >= 0 ? o : SendPropOffset(raw, field);
}

int CachedFieldOffset(PropSlot &slot, const void *raw, const char *name, bool sendOnly)
{
	if (!raw)
		return -1;
	const void *key = VCall<datamap_t *>(raw, vt::GetDataDescMap);   // unique per class
	if (slot.offset != -2 && slot.dmap == key)
		return slot.offset;
	slot.dmap = key;
	slot.offset = sendOnly ? SendPropOffset(raw, name) : FieldOffset(raw, name);
	return slot.offset;
}

void ResetPropCache() {}

} // namespace pf2
