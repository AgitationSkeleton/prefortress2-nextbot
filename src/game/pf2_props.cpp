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

int SendPropOffset(const void *raw, const char *prop)
{
	if (!raw)
		return -1;
	ServerClass *sc = VCall<ServerClass *>(raw, vt::GetServerClass);
	return sc && sc->m_pTable ? SearchSendTable(sc->m_pTable, prop, 0) : -1;
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
	return SearchDataMap(map, field, 0);
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
