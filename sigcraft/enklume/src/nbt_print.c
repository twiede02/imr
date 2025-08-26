#include "enklume/nbt.h"
#include "support_private.h"

#pragma GCC diagnostic error "-Wswitch"

static const char* nbt_tag_name[] = {
"End",
#define T(name, _) #name,
NBT_TAG_TYPES(T)
#undef T
};

#include <string.h>
#include <stdarg.h>

static void enkl_print(Enkl_Printer* p, const char* str, ...) {
    va_list args;
    va_start(args, str);
    char* fstr = enkl_format_string_valist(str, args);
    va_end(args);
    p->append(p, fstr, strlen(fstr));
}

static void enkl_print_nbt_body(Enkl_Printer* p, NBT_Tag tag, NBT_Body body) {
    switch (tag) {
        case NBT_Tag_End:    enkl_print(p, "END"); return;
        case NBT_Tag_Byte:   enkl_print(p, "%d", body.p_byte); break;
        case NBT_Tag_Short:  enkl_print(p, "%d", body.p_short); break;
        case NBT_Tag_Int:    enkl_print(p, "%d", body.p_int); break;
        case NBT_Tag_Long:   enkl_print(p, "%d", body.p_long); break;
        case NBT_Tag_Float:  enkl_print(p, "%f", body.p_float); break;
        case NBT_Tag_Double: enkl_print(p, "%f", body.p_double); break;
        case NBT_Tag_ByteArray:
            enkl_print(p, "[");
            for (int32_t i = 0; i < body.p_byte_array.count; i++) {
                enkl_print(p, "%d", body.p_byte_array.arr[i]);
                if (i + 1 < body.p_byte_array.count)
                    enkl_print(p, ", ");
            }
            enkl_print(p, "]");
            break;
        case NBT_Tag_String:
            enkl_print(p, "\"%s\"", body.p_string);
            break;
        case NBT_Tag_List:
            enkl_print(p, "(%s[]) [", nbt_tag_name[body.p_list.tag]);
            for (int32_t i = 0; i < body.p_list.count; i++) {
                enkl_print_nbt_body(p, body.p_list.tag, body.p_list.bodies[i]);
                if (i + 1 < body.p_list.count)
                    enkl_print(p, ", ");
            }
            enkl_print(p, "]");
            break;
        case NBT_Tag_Compound:
            enkl_print(p, "{");
            p->indent(p);
            p->newline(p);
            for (int32_t i = 0; i < body.p_compound.count; i++) {
                enkl_print_nbt(p, body.p_compound.objects[i]);
                if (i + 1 < body.p_compound.count)
                    p->newline(p);
            }
            p->deindent(p);
            p->newline(p);
            enkl_print(p, "}");
            break;
        case NBT_Tag_IntArray:
            enkl_print(p, "[");
            for (int32_t i = 0; i < body.p_int_array.count; i++) {
                enkl_print(p, "%d", body.p_int_array.arr[i]);
                if (i + 1 < body.p_int_array.count)
                    enkl_print(p, ", ");
            }
            enkl_print(p, "]");
            break;
        case NBT_Tag_LongArray:
            enkl_print(p, "[");
            for (int32_t i = 0; i < body.p_long_array.count; i++) {
                enkl_print(p, "%d", body.p_long_array.arr[i]);
                if (i + 1 < body.p_long_array.count)
                    enkl_print(p, ", ");
            }
            enkl_print(p, "]");
            break;
    }
}

void enkl_print_nbt(Enkl_Printer* p, const NBT_Object* o) {
    enkl_print(p, "%s %s = ", nbt_tag_name[o->tag], o->name);
    enkl_print_nbt_body(p, o->tag, o->body);
}
