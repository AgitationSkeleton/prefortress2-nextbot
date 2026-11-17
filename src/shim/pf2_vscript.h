// pf2-nextbot game layer: VScript is not available in PF2. The 2025 SDK NextBot/nav/TFBot sources
// carry VScript bindings; these stubs let them compile. Nothing is ever registered or called.
#pragma once

struct HSCRIPT__;
typedef HSCRIPT__ *HSCRIPT;
#define INVALID_HSCRIPT ((HSCRIPT)NULL)
inline bool IsValid(HSCRIPT h) { return h != NULL; }

struct ScriptClassDesc_t {};
struct ScriptVariant_t
{
	ScriptVariant_t() {}
	template <typename T> ScriptVariant_t(const T &) {}
};

class CPF2NullScriptVM
{
public:
	template <typename... A> bool SetValue(A...) { return false; }
	template <typename... A> HSCRIPT RegisterInstance(A...) { return NULL; }
	template <typename... A> void RemoveInstance(A...) {}
	template <typename... A> void *GetInstanceValue(A...) { return NULL; }
	template <typename... A> HSCRIPT CreateTable(A...) { return NULL; }
	template <typename... A> HSCRIPT CreateArray(A...) { return NULL; }
	template <typename... A> void ReleaseValue(A...) {}
	template <typename... A> bool ArrayAppend(A...) { return false; }
};
extern CPF2NullScriptVM *g_pScriptVM;

template <typename T> ScriptClassDesc_t *GetScriptDesc(T *) { return NULL; }
#define GetScriptDescForClass(className) ((ScriptClassDesc_t *)NULL)

#define DECLARE_ENT_SCRIPTDESC() virtual ScriptClassDesc_t *GetScriptDesc() { return NULL; }
#define DECLARE_SCRIPTDESC()
#define ALLOW_SCRIPT_ACCESS()
#define BEGIN_ENT_SCRIPTDESC(...)
#define BEGIN_ENT_SCRIPTDESC_ROOT(...)
#define BEGIN_SCRIPTDESC_ROOT(...)
#define BEGIN_SCRIPTDESC(...)
#define END_SCRIPTDESC()
#define DEFINE_SCRIPTFUNC(...)
#define DEFINE_SCRIPTFUNC_NAMED(...)
#define DEFINE_SCRIPTFUNC_WRAPPED(...)
#define DEFINE_SCRIPT_INSTANCE_HELPER(...)
#define DEFINE_SCRIPT_CONSTRUCTOR()
#define DEFINE_SCRIPTENUM(...)

class CBaseEntity;
inline HSCRIPT ToHScript(CBaseEntity *) { return NULL; }
inline CBaseEntity *ToEnt(HSCRIPT) { return NULL; }
