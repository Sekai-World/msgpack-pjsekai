#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define INITIAL_CAP 16
#define GENERATOR_VERSION "0.4.0"
/* Package-manager release version for wrappers generated from the current dump.cs. */
#define PACKAGE_VERSION "6.5.0"
#define JS_PACKAGE_NAME "msgpack-pjsekai"
#define PYTHON_PACKAGE_NAME "msgpack-pjsekai"
#define GO_MODULE_PATH "github.com/Sekai-World/msgpack-pjsekai/msgpack/wrappers/go"
#define JAVA_PACKAGE_NAME "io.github.sekaiworld.msgpackpjsekai"
#define JAVA_PACKAGE_PATH "io/github/sekaiworld/msgpackpjsekai"
#define MAVEN_GROUP_ID "io.github.sekai-world"
#define MAVEN_ARTIFACT_ID "msgpack-pjsekai-java"
#define PROJECT_URL "https://github.com/Sekai-World/msgpack-pjsekai"

typedef enum {
    KEY_STRING,
    KEY_INT
} KeyKind;

typedef struct {
    char *cs_type;
    char *name;
    char *c_name;
    KeyKind key_kind;
    char *key_string;
    long key_int;
} Member;

typedef struct {
    char *name;
    char *ns;
    char *c_name;
    Member *members;
    size_t member_count;
    size_t member_cap;
} Class;

typedef struct {
    Class *classes;
    size_t class_count;
    size_t class_cap;
} Model;

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    memcpy(p, s, n);
    return p;
}

static char *xstrndup(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

static bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void strip_line_comment(char *s) {
    for (char *p = s; *p; ++p) {
        if (p[0] == '/' && p[1] == '/') {
            *p = '\0';
            return;
        }
    }
}

static char *sanitize_ident(const char *s) {
    size_t n = strlen(s);
    char *out = (char *)malloc(n * 2 + 2);
    if (!out) exit(2);
    size_t j = 0;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) out[j++] = '_';
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        out[j++] = (char)((isalnum(c) || c == '_') ? c : '_');
    }
    out[j] = '\0';
    return out;
}

static char *class_c_name(const char *ns, const char *name) {
    if (ns && *ns) {
        size_t len = strlen(ns) + 1 + strlen(name) + 1;
        char *tmp = (char *)malloc(len);
        if (!tmp) exit(2);
        snprintf(tmp, len, "%s_%s", ns, name);
        char *out = sanitize_ident(tmp);
        free(tmp);
        return out;
    }
    return sanitize_ident(name);
}

static void model_add_class(Model *m, Class c) {
    if (m->class_count == m->class_cap) {
        m->class_cap = m->class_cap ? m->class_cap * 2 : INITIAL_CAP;
        m->classes = (Class *)realloc(m->classes, m->class_cap * sizeof(Class));
        if (!m->classes) exit(2);
    }
    m->classes[m->class_count++] = c;
}

static void class_add_member(Class *c, Member mem) {
    if (c->member_count == c->member_cap) {
        c->member_cap = c->member_cap ? c->member_cap * 2 : INITIAL_CAP;
        c->members = (Member *)realloc(c->members, c->member_cap * sizeof(Member));
        if (!c->members) exit(2);
    }
    c->members[c->member_count++] = mem;
}

static const char *find_type_keyword(const char *line, const char **kind) {
    const char *keywords[] = {" class ", " struct ", " interface "};
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        const char *p = strstr(line, keywords[i]);
        if (p) {
            *kind = keywords[i];
            return p + strlen(keywords[i]);
        }
    }
    if (starts_with(line, "public class ")) { *kind = " class "; return line + 13; }
    if (starts_with(line, "public struct ")) { *kind = " struct "; return line + 14; }
    return NULL;
}

static char *extract_type_name(const char *line) {
    const char *kind = NULL;
    const char *p = find_type_keyword(line, &kind);
    (void)kind;
    if (!p) return NULL;
    const char *start = p;
    while (*p && !isspace((unsigned char)*p) && *p != ':' && *p != '<' && *p != '{') p++;
    return xstrndup(start, (size_t)(p - start));
}

static bool parse_key_attr(const char *line, KeyKind *kind, char **skey, long *ikey) {
    const char *p = strstr(line, "[Key(");
    if (!p) return false;
    p += 5;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '"') {
        p++;
        const char *start = p;
        while (*p && *p != '"') p++;
        *kind = KEY_STRING;
        *skey = xstrndup(start, (size_t)(p - start));
        return true;
    }
    errno = 0;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end != p && errno == 0) {
        *kind = KEY_INT;
        *ikey = v;
        *skey = NULL;
        return true;
    }
    return false;
}

static bool is_modifier(const char *tok) {
    return strcmp(tok, "readonly") == 0 || strcmp(tok, "volatile") == 0 ||
           strcmp(tok, "new") == 0 || strcmp(tok, "virtual") == 0 ||
           strcmp(tok, "override") == 0 || strcmp(tok, "sealed") == 0;
}

static bool parse_member_line(char *line, Member *out, KeyKind key_kind, char *key_string, long key_int) {
    strip_line_comment(line);
    char *s = trim(line);
    if (!starts_with(s, "public ")) return false;
    s += 7;
    while (true) {
        char *sp = s;
        while (*sp && !isspace((unsigned char)*sp)) sp++;
        char save = *sp;
        *sp = '\0';
        bool mod = is_modifier(s);
        *sp = save;
        if (!mod) break;
        s = trim(sp);
    }
    if (starts_with(s, "const ") || starts_with(s, "static ") || starts_with(s, "event ")) return false;

    char *end = strchr(s, ';');
    char *brace = strchr(s, '{');
    if (!end || (brace && brace < end)) end = brace;
    if (!end) return false;

    char *paren = strchr(s, '(');
    if (paren && paren < end) return false;

    char *decl = xstrndup(s, (size_t)(end - s));
    char *d = trim(decl);
    int depth = 0;
    char *last_ws = NULL;
    for (char *p = d; *p; ++p) {
        if (*p == '<') depth++;
        else if (*p == '>' && depth > 0) depth--;
        else if (isspace((unsigned char)*p) && depth == 0) last_ws = p;
    }
    if (!last_ws) { free(decl); return false; }
    *last_ws = '\0';
    char *type = trim(d);
    char *name = trim(last_ws + 1);
    if (*type == '\0' || *name == '\0') { free(decl); return false; }
    if (strchr(name, '(')) { free(decl); return false; }

    out->cs_type = xstrdup(type);
    out->name = xstrdup(name);
    out->c_name = sanitize_ident(name);
    out->key_kind = key_kind;
    out->key_string = key_string;
    out->key_int = key_int;
    free(decl);
    return true;
}

static bool is_integer_type(const char *t) {
    return strcmp(t, "sbyte") == 0 || strcmp(t, "byte") == 0 || strcmp(t, "short") == 0 ||
           strcmp(t, "ushort") == 0 || strcmp(t, "int") == 0 || strcmp(t, "uint") == 0 ||
           strcmp(t, "long") == 0 || strcmp(t, "ulong") == 0;
}

static bool is_unsigned_type(const char *t) {
    return strcmp(t, "byte") == 0 || strcmp(t, "ushort") == 0 || strcmp(t, "uint") == 0 || strcmp(t, "ulong") == 0;
}

static const char *c_type_for(const char *t) {
    if (strcmp(t, "bool") == 0) return "bool";
    if (strcmp(t, "sbyte") == 0) return "int8_t";
    if (strcmp(t, "byte") == 0) return "uint8_t";
    if (strcmp(t, "short") == 0) return "int16_t";
    if (strcmp(t, "ushort") == 0) return "uint16_t";
    if (strcmp(t, "int") == 0) return "int32_t";
    if (strcmp(t, "uint") == 0) return "uint32_t";
    if (strcmp(t, "long") == 0) return "int64_t";
    if (strcmp(t, "ulong") == 0) return "uint64_t";
    if (strcmp(t, "float") == 0) return "float";
    if (strcmp(t, "double") == 0) return "double";
    if (strcmp(t, "string") == 0 || strcmp(t, "String") == 0) return "char *";
    return "msgpack_object";
}

static const char *ts_type_for(const char *t) {
    if (strcmp(t, "bool") == 0) return "boolean";
    if (is_integer_type(t) || strcmp(t, "float") == 0 || strcmp(t, "double") == 0) return "number";
    if (strcmp(t, "string") == 0 || strcmp(t, "String") == 0) return "string | null";
    return "unknown";
}

static const char *go_object_type_for(const char *t) {
    if (strcmp(t, "bool") == 0) return "bool";
    if (strcmp(t, "sbyte") == 0) return "int8";
    if (strcmp(t, "byte") == 0) return "uint8";
    if (strcmp(t, "short") == 0) return "int16";
    if (strcmp(t, "ushort") == 0) return "uint16";
    if (strcmp(t, "int") == 0) return "int32";
    if (strcmp(t, "uint") == 0) return "uint32";
    if (strcmp(t, "long") == 0) return "int64";
    if (strcmp(t, "ulong") == 0) return "uint64";
    if (strcmp(t, "float") == 0) return "float32";
    if (strcmp(t, "double") == 0) return "float64";
    if (strcmp(t, "string") == 0 || strcmp(t, "String") == 0) return "string";
    return "any";
}

static const char *java_object_type_for(const char *t) {
    if (strcmp(t, "bool") == 0) return "Boolean";
    if (is_integer_type(t)) return "Long";
    if (strcmp(t, "float") == 0 || strcmp(t, "double") == 0) return "Double";
    if (strcmp(t, "string") == 0 || strcmp(t, "String") == 0) return "String";
    return "Object";
}

static const char *go_decode_helper_for(const char *t) {
    if (strcmp(t, "bool") == 0) return "toBool";
    if (strcmp(t, "sbyte") == 0) return "toInt8";
    if (strcmp(t, "byte") == 0) return "toUint8";
    if (strcmp(t, "short") == 0) return "toInt16";
    if (strcmp(t, "ushort") == 0) return "toUint16";
    if (strcmp(t, "int") == 0) return "toInt32";
    if (strcmp(t, "uint") == 0) return "toUint32";
    if (strcmp(t, "long") == 0) return "toInt64";
    if (strcmp(t, "ulong") == 0) return "toUint64";
    if (strcmp(t, "float") == 0) return "toFloat32";
    if (strcmp(t, "double") == 0) return "toFloat64";
    if (strcmp(t, "string") == 0 || strcmp(t, "String") == 0) return "toString";
    return NULL;
}

static const char *python_optional_type_for(const char *t) {
    if (strcmp(t, "bool") == 0) return "Optional[bool]";
    if (is_integer_type(t)) return "Optional[int]";
    if (strcmp(t, "float") == 0 || strcmp(t, "double") == 0) return "Optional[float]";
    if (strcmp(t, "string") == 0 || strcmp(t, "String") == 0) return "Optional[str]";
    return "Any";
}

static const char *java_decode_helper_for(const char *t) {
    if (strcmp(t, "bool") == 0) return "asBoolean";
    if (is_integer_type(t)) return "asLong";
    if (strcmp(t, "float") == 0 || strcmp(t, "double") == 0) return "asDouble";
    if (strcmp(t, "string") == 0 || strcmp(t, "String") == 0) return "asString";
    return "asObject";
}

static char *export_ident(const char *s) {
    char *out = sanitize_ident(s);
    if (out[0] == '_') {
        size_t n = strlen(out);
        char *tmp = (char *)malloc(n + 2);
        if (!tmp) exit(2);
        tmp[0] = 'X';
        memcpy(tmp + 1, out, n + 1);
        free(out);
        out = tmp;
    }
    if (out[0]) out[0] = (char)toupper((unsigned char)out[0]);
    return out;
}

static void fprint_c_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '\\' || c == '"') fprintf(f, "\\%c", c);
        else if (c == '\n') fprintf(f, "\\n");
        else if (c == '\r') fprintf(f, "\\r");
        else if (c == '\t') fprintf(f, "\\t");
        else if (c < 32 || c > 126) fprintf(f, "\\x%02x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static void fprint_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '\\' || c == '"') fprintf(f, "\\%c", c);
        else if (c == '\n') fprintf(f, "\\n");
        else if (c == '\r') fprintf(f, "\\r");
        else if (c == '\t') fprintf(f, "\\t");
        else if (c < 32) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static void write_preamble(FILE *f, const char *source_name) {
    fprintf(f, "/*\n");
    fprintf(f, " * DO NOT EDIT!\n");
    fprintf(f, " * Generated by msgpack-pjsekai dump.cs compiler version %s.\n", GENERATOR_VERSION);
    fprintf(f, " * source: %s\n", source_name ? source_name : "dump.cs");
    fprintf(f, " */\n\n");
}

static void write_hash_preamble(FILE *f, const char *source_name) {
    fprintf(f, "# DO NOT EDIT!\n");
    fprintf(f, "# Generated by msgpack-pjsekai dump.cs compiler version %s.\n", GENERATOR_VERSION);
    fprintf(f, "# source: %s\n\n", source_name ? source_name : "dump.cs");
}

static int ensure_dir(const char *path) {
    char tmp[4096];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        fprintf(stderr, "invalid directory path: %s\n", path);
        return 1;
    }
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "failed to create directory %s: %s\n", tmp, strerror(errno));
            return 1;
        }
        *p = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "failed to create directory %s: %s\n", tmp, strerror(errno));
        return 1;
    }
    return 0;
}

static int open_generated(FILE **out, const char *path, const char *source_name) {
    *out = fopen(path, "w");
    if (!*out) {
        fprintf(stderr, "failed to write %s: %s\n", path, strerror(errno));
        return 1;
    }
    write_preamble(*out, source_name);
    return 0;
}

static int open_generated_hash(FILE **out, const char *path, const char *source_name) {
    *out = fopen(path, "w");
    if (!*out) {
        fprintf(stderr, "failed to write %s: %s\n", path, strerror(errno));
        return 1;
    }
    write_hash_preamble(*out, source_name);
    return 0;
}

static int open_xml(FILE **out, const char *path) {
    *out = fopen(path, "w");
    if (!*out) {
        fprintf(stderr, "failed to write %s: %s\n", path, strerror(errno));
        return 1;
    }
    fprintf(*out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    return 0;
}

static int open_plain(FILE **out, const char *path) {
    *out = fopen(path, "w");
    if (!*out) {
        fprintf(stderr, "failed to write %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}

static void generate_common_header(FILE *f) {
    fprintf(f, "#ifndef MSGPACK_PJSEKAI_COMMON_GENERATED_H\n#define MSGPACK_PJSEKAI_COMMON_GENERATED_H\n\n");
    fprintf(f, "#include <stdint.h>\n#include <msgpack.h>\n\n");
    fprintf(f, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    fprintf(f, "int mpj_key_eq_str(const msgpack_object *key, const char *s);\n");
    fprintf(f, "int mpj_key_eq_int(const msgpack_object *key, int64_t v);\n");
    fprintf(f, "char *mpj_copy_msgpack_str(const msgpack_object *obj);\n\n");
    fprintf(f, "#ifdef __cplusplus\n}\n#endif\n\n#endif\n");
}

static void generate_common_source(FILE *f) {
    fprintf(f, "#include \"msgpack-pjsekai-common.h\"\n\n");
    fprintf(f, "#include <stdbool.h>\n#include <stdlib.h>\n#include <string.h>\n\n");
    fprintf(f, "int mpj_key_eq_str(const msgpack_object *key, const char *s) {\n");
    fprintf(f, "    size_t len = strlen(s);\n");
    fprintf(f, "    return key->type == MSGPACK_OBJECT_STR && key->via.str.size == len && memcmp(key->via.str.ptr, s, len) == 0;\n}\n\n");
    fprintf(f, "int mpj_key_eq_int(const msgpack_object *key, int64_t v) {\n");
    fprintf(f, "    if (key->type == MSGPACK_OBJECT_POSITIVE_INTEGER) return key->via.u64 == (uint64_t)v;\n");
    fprintf(f, "    if (key->type == MSGPACK_OBJECT_NEGATIVE_INTEGER) return key->via.i64 == v;\n");
    fprintf(f, "    return 0;\n}\n\n");
    fprintf(f, "char *mpj_copy_msgpack_str(const msgpack_object *obj) {\n");
    fprintf(f, "    if (obj->type != MSGPACK_OBJECT_STR) return NULL;\n");
    fprintf(f, "    char *s = (char *)malloc(obj->via.str.size + 1);\n");
    fprintf(f, "    if (!s) return NULL;\n");
    fprintf(f, "    memcpy(s, obj->via.str.ptr, obj->via.str.size);\n");
    fprintf(f, "    s[obj->via.str.size] = '\\0';\n");
    fprintf(f, "    return s;\n}\n");
}

static void generate_class_header(FILE *f, const Class *c) {
    fprintf(f, "#ifndef MSGPACK_PJSEKAI_%s_GENERATED_H\n#define MSGPACK_PJSEKAI_%s_GENERATED_H\n\n", c->c_name, c->c_name);
    fprintf(f, "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n#include <msgpack.h>\n\n");
    fprintf(f, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    fprintf(f, "typedef struct %s {\n", c->c_name);
    if (c->member_count == 0) {
        fprintf(f, "    uint8_t _empty;\n");
    }
    for (size_t j = 0; j < c->member_count; ++j) {
        const Member *mem = &c->members[j];
        fprintf(f, "    bool has_%s;\n", mem->c_name);
        fprintf(f, "    %s %s;\n", c_type_for(mem->cs_type), mem->c_name);
    }
    fprintf(f, "} %s;\n\n", c->c_name);
    fprintf(f, "int %s_pack(msgpack_packer *pk, const %s *value);\n", c->c_name, c->c_name);
    fprintf(f, "int %s_unpack(const msgpack_object *obj, %s *out);\n", c->c_name, c->c_name);
    fprintf(f, "void %s_free(%s *value);\n\n", c->c_name, c->c_name);
    fprintf(f, "#ifdef __cplusplus\n}\n#endif\n\n#endif\n");
}

static void generate_class_source(FILE *f, const Class *c) {
    fprintf(f, "#include \"%s.h\"\n", c->c_name);
    fprintf(f, "#include \"msgpack-pjsekai-common.h\"\n\n");
    fprintf(f, "#include <stdlib.h>\n#include <string.h>\n\n");

    fprintf(f, "int %s_pack(msgpack_packer *pk, const %s *value) {\n", c->c_name, c->c_name);
    fprintf(f, "    if (!pk || !value) return -1;\n");
    fprintf(f, "    uint32_t count = 0;\n");
    for (size_t j = 0; j < c->member_count; ++j) fprintf(f, "    if (value->has_%s) count++;\n", c->members[j].c_name);
    fprintf(f, "    msgpack_pack_map(pk, count);\n");
    for (size_t j = 0; j < c->member_count; ++j) {
        const Member *mem = &c->members[j];
        fprintf(f, "    if (value->has_%s) {\n", mem->c_name);
        if (mem->key_kind == KEY_STRING) {
            fprintf(f, "        msgpack_pack_str(pk, %zu);\n", strlen(mem->key_string));
            fprintf(f, "        msgpack_pack_str_body(pk, ");
            fprint_c_string(f, mem->key_string);
            fprintf(f, ", %zu);\n", strlen(mem->key_string));
        } else {
            fprintf(f, "        msgpack_pack_int64(pk, %ld);\n", mem->key_int);
        }
        const char *t = mem->cs_type;
        if (strcmp(t, "bool") == 0) {
            fprintf(f, "        if (value->%s) msgpack_pack_true(pk); else msgpack_pack_false(pk);\n", mem->c_name);
        } else if (is_integer_type(t)) {
            fprintf(f, "        msgpack_pack_%s(pk, value->%s);\n", is_unsigned_type(t) ? "uint64" : "int64", mem->c_name);
        } else if (strcmp(t, "float") == 0) {
            fprintf(f, "        msgpack_pack_float(pk, value->%s);\n", mem->c_name);
        } else if (strcmp(t, "double") == 0) {
            fprintf(f, "        msgpack_pack_double(pk, value->%s);\n", mem->c_name);
        } else if (strcmp(t, "string") == 0 || strcmp(t, "String") == 0) {
            fprintf(f, "        if (value->%s) { size_t len = strlen(value->%s); msgpack_pack_str(pk, len); msgpack_pack_str_body(pk, value->%s, len); } else { msgpack_pack_nil(pk); }\n", mem->c_name, mem->c_name, mem->c_name);
        } else {
            fprintf(f, "        msgpack_pack_object(pk, value->%s);\n", mem->c_name);
        }
        fprintf(f, "    }\n");
    }
    fprintf(f, "    return 0;\n}\n\n");

    fprintf(f, "int %s_unpack(const msgpack_object *obj, %s *out) {\n", c->c_name, c->c_name);
    fprintf(f, "    if (!obj || !out || obj->type != MSGPACK_OBJECT_MAP) return -1;\n");
    fprintf(f, "    memset(out, 0, sizeof(*out));\n");
    if (c->member_count > 0) {
        fprintf(f, "    for (uint32_t i = 0; i < obj->via.map.size; ++i) {\n");
        fprintf(f, "        const msgpack_object *key = &obj->via.map.ptr[i].key;\n");
        fprintf(f, "        const msgpack_object *val = &obj->via.map.ptr[i].val;\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "        %s (", j == 0 ? "if" : "else if");
            if (mem->key_kind == KEY_STRING) {
                fprintf(f, "mpj_key_eq_str(key, ");
                fprint_c_string(f, mem->key_string);
                fprintf(f, ")");
            } else {
                fprintf(f, "mpj_key_eq_int(key, %ld)", mem->key_int);
            }
            fprintf(f, ") {\n");
            const char *t = mem->cs_type;
            if (strcmp(t, "bool") == 0) {
                fprintf(f, "            if (val->type == MSGPACK_OBJECT_BOOLEAN) { out->%s = val->via.boolean; out->has_%s = true; }\n", mem->c_name, mem->c_name);
            } else if (is_integer_type(t)) {
                if (is_unsigned_type(t)) {
                    fprintf(f, "            if (val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) { out->%s = (%s)val->via.u64; out->has_%s = true; }\n", mem->c_name, c_type_for(t), mem->c_name);
                } else {
                    fprintf(f, "            if (val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) { out->%s = (%s)val->via.u64; out->has_%s = true; }\n", mem->c_name, c_type_for(t), mem->c_name);
                    fprintf(f, "            else if (val->type == MSGPACK_OBJECT_NEGATIVE_INTEGER) { out->%s = (%s)val->via.i64; out->has_%s = true; }\n", mem->c_name, c_type_for(t), mem->c_name);
                }
            } else if (strcmp(t, "float") == 0) {
                fprintf(f, "            if (val->type == MSGPACK_OBJECT_FLOAT32 || val->type == MSGPACK_OBJECT_FLOAT64) { out->%s = (float)val->via.f64; out->has_%s = true; }\n", mem->c_name, mem->c_name);
            } else if (strcmp(t, "double") == 0) {
                fprintf(f, "            if (val->type == MSGPACK_OBJECT_FLOAT32 || val->type == MSGPACK_OBJECT_FLOAT64) { out->%s = val->via.f64; out->has_%s = true; }\n", mem->c_name, mem->c_name);
            } else if (strcmp(t, "string") == 0 || strcmp(t, "String") == 0) {
                fprintf(f, "            if (val->type == MSGPACK_OBJECT_NIL) { out->%s = NULL; out->has_%s = true; }\n", mem->c_name, mem->c_name);
                fprintf(f, "            else if (val->type == MSGPACK_OBJECT_STR) { out->%s = mpj_copy_msgpack_str(val); out->has_%s = (out->%s != NULL); }\n", mem->c_name, mem->c_name, mem->c_name);
            } else {
                fprintf(f, "            out->%s = *val; out->has_%s = true;\n", mem->c_name, mem->c_name);
            }
            fprintf(f, "        }\n");
        }
        fprintf(f, "    }\n");
    }
    fprintf(f, "    return 0;\n}\n\n");

    fprintf(f, "void %s_free(%s *value) {\n", c->c_name, c->c_name);
    if (c->member_count == 0) {
        fprintf(f, "    (void)value;\n");
    } else {
        fprintf(f, "    if (!value) return;\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            if (strcmp(mem->cs_type, "string") == 0 || strcmp(mem->cs_type, "String") == 0) {
                fprintf(f, "    free(value->%s);\n    value->%s = NULL;\n    value->has_%s = false;\n", mem->c_name, mem->c_name, mem->c_name);
            }
        }
    }
    fprintf(f, "}\n");
}

static void generate_umbrella_header(FILE *f, const Model *m) {
    fprintf(f, "#ifndef MSGPACK_PJSEKAI_GENERATED_H\n#define MSGPACK_PJSEKAI_GENERATED_H\n\n");
    fprintf(f, "#include \"generated/msgpack-pjsekai-common.h\"\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        fprintf(f, "#include \"generated/%s.h\"\n", m->classes[i].c_name);
    }
    fprintf(f, "\n#endif\n");
}

static void generate_bridge_header(FILE *f, const Model *m) {
    (void)m;
    fprintf(f, "#ifndef MSGPACK_PJSEKAI_BRIDGE_GENERATED_H\n#define MSGPACK_PJSEKAI_BRIDGE_GENERATED_H\n\n");
    fprintf(f, "#include <stddef.h>\n#include <stdint.h>\n\n");
    fprintf(f, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    fprintf(f, "typedef struct mpj_buffer {\n    uint8_t *data;\n    size_t size;\n} mpj_buffer;\n\n");
    fprintf(f, "typedef struct mpj_value mpj_value;\n\n");
    fprintf(f, "enum {\n    MPJ_KEY_STRING = 0,\n    MPJ_KEY_INT = 1\n};\n\n");
    fprintf(f, "enum {\n    MPJ_VALUE_NIL = 0,\n    MPJ_VALUE_BOOL = 1,\n    MPJ_VALUE_INT = 2,\n    MPJ_VALUE_UINT = 3,\n    MPJ_VALUE_FLOAT = 4,\n    MPJ_VALUE_STRING = 5,\n    MPJ_VALUE_BINARY = 6,\n    MPJ_VALUE_ARRAY = 7,\n    MPJ_VALUE_MAP = 8\n};\n\n");
    fprintf(f, "const uint8_t *mpj_buffer_data(const mpj_buffer *buffer);\n");
    fprintf(f, "size_t mpj_buffer_size(const mpj_buffer *buffer);\n");
    fprintf(f, "void mpj_buffer_delete(mpj_buffer *buffer);\n\n");
    fprintf(f, "size_t mpj_type_count(void);\n");
    fprintf(f, "const char *mpj_type_name(size_t type_id);\n");
    fprintf(f, "int mpj_type_id(const char *type_name);\n");
    fprintf(f, "size_t mpj_field_count(int type_id);\n");
    fprintf(f, "const char *mpj_field_name(int type_id, size_t field_id);\n");
    fprintf(f, "const char *mpj_field_c_type(int type_id, size_t field_id);\n");
    fprintf(f, "int mpj_field_key_kind(int type_id, size_t field_id);\n");
    fprintf(f, "const char *mpj_field_key_string(int type_id, size_t field_id);\n");
    fprintf(f, "int64_t mpj_field_key_int(int type_id, size_t field_id);\n");
    fprintf(f, "void *mpj_new(int type_id);\n");
    fprintf(f, "int mpj_delete(int type_id, void *value);\n");
    fprintf(f, "mpj_buffer *mpj_pack_bytes(int type_id, const void *value);\n");
    fprintf(f, "int mpj_unpack_bytes(int type_id, const uint8_t *data, size_t size, void *out);\n\n");
    fprintf(f, "mpj_value *mpj_value_new_nil(void);\n");
    fprintf(f, "mpj_value *mpj_value_new_bool(int value);\n");
    fprintf(f, "mpj_value *mpj_value_new_int(int64_t value);\n");
    fprintf(f, "mpj_value *mpj_value_new_uint(uint64_t value);\n");
    fprintf(f, "mpj_value *mpj_value_new_float(double value);\n");
    fprintf(f, "mpj_value *mpj_value_new_number(double value);\n");
    fprintf(f, "mpj_value *mpj_value_new_string(const uint8_t *data, size_t size);\n");
    fprintf(f, "mpj_value *mpj_value_new_binary(const uint8_t *data, size_t size);\n");
    fprintf(f, "mpj_value *mpj_value_new_array(size_t size);\n");
    fprintf(f, "mpj_value *mpj_value_new_map(size_t size);\n");
    fprintf(f, "void mpj_value_free(mpj_value *value);\n");
    fprintf(f, "int mpj_value_array_set(mpj_value *array, size_t index, mpj_value *item);\n");
    fprintf(f, "int mpj_value_map_set(mpj_value *map, size_t index, mpj_value *key, mpj_value *value);\n");
    fprintf(f, "mpj_buffer *mpj_value_pack_bytes(const mpj_value *value);\n");
    fprintf(f, "mpj_value *mpj_value_unpack_bytes(const uint8_t *data, size_t size);\n");
    fprintf(f, "int mpj_value_kind(const mpj_value *value);\n");
    fprintf(f, "int mpj_value_bool(const mpj_value *value);\n");
    fprintf(f, "int64_t mpj_value_int(const mpj_value *value);\n");
    fprintf(f, "uint64_t mpj_value_uint(const mpj_value *value);\n");
    fprintf(f, "double mpj_value_float(const mpj_value *value);\n");
    fprintf(f, "double mpj_value_number(const mpj_value *value);\n");
    fprintf(f, "const uint8_t *mpj_value_data(const mpj_value *value);\n");
    fprintf(f, "size_t mpj_value_size(const mpj_value *value);\n");
    fprintf(f, "const mpj_value *mpj_value_array_get(const mpj_value *array, size_t index);\n");
    fprintf(f, "const mpj_value *mpj_value_map_key(const mpj_value *map, size_t index);\n");
    fprintf(f, "const mpj_value *mpj_value_map_value(const mpj_value *map, size_t index);\n\n");
    fprintf(f, "#ifdef __cplusplus\n}\n#endif\n\n#endif\n");
}

static void generate_bridge_source(FILE *f, const Model *m) {
    fprintf(f, "#include \"msgpack-pjsekai-bridge.h\"\n");
    fprintf(f, "#include \"../msgpack-pjsekai.h\"\n\n");
    fprintf(f, "#include <stdlib.h>\n#include <string.h>\n\n");
    fprintf(f, "typedef void (*mpj_free_fn)(void *value);\n");
    fprintf(f, "typedef int (*mpj_pack_fn)(msgpack_packer *pk, const void *value);\n");
    fprintf(f, "typedef int (*mpj_unpack_fn)(const msgpack_object *obj, void *out);\n\n");
    fprintf(f, "typedef struct mpj_field_entry {\n    const char *name;\n    const char *c_type;\n    int key_kind;\n    const char *key_string;\n    int64_t key_int;\n} mpj_field_entry;\n\n");
    fprintf(f, "typedef struct mpj_type_entry {\n    const char *name;\n    size_t size;\n    mpj_free_fn free_fn;\n    mpj_pack_fn pack_fn;\n    mpj_unpack_fn unpack_fn;\n    const mpj_field_entry *fields;\n    size_t field_count;\n} mpj_type_entry;\n\n");
    fprintf(f, "#define MPJ_DEFINE_ADAPTERS(T) \\\n    static void T##_free_dyn(void *value) { T##_free((T *)value); } \\\n    static int T##_pack_dyn(msgpack_packer *pk, const void *value) { return T##_pack(pk, (const T *)value); } \\\n    static int T##_unpack_dyn(const msgpack_object *obj, void *out) { return T##_unpack(obj, (T *)out); }\n\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        fprintf(f, "MPJ_DEFINE_ADAPTERS(%s)\n", m->classes[i].c_name);
    }
    fprintf(f, "\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        if (c->member_count == 0) continue;
        fprintf(f, "static const mpj_field_entry %s_fields[] = {\n", c->c_name);
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "    { ");
            fprint_c_string(f, mem->c_name);
            fprintf(f, ", ");
            fprint_c_string(f, c_type_for(mem->cs_type));
            fprintf(f, ", %s, ", mem->key_kind == KEY_INT ? "MPJ_KEY_INT" : "MPJ_KEY_STRING");
            if (mem->key_kind == KEY_STRING) fprint_c_string(f, mem->key_string);
            else fprintf(f, "NULL");
            fprintf(f, ", %ld },\n", mem->key_int);
        }
        fprintf(f, "};\n\n");
    }
    fprintf(f, "\nstatic const mpj_type_entry mpj_types[] = {\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        const char *name = c->c_name;
        if (c->member_count > 0) {
            fprintf(f, "    { \"%s\", sizeof(%s), %s_free_dyn, %s_pack_dyn, %s_unpack_dyn, %s_fields, %zu },\n",
                    name, name, name, name, name, name, c->member_count);
        } else {
            fprintf(f, "    { \"%s\", sizeof(%s), %s_free_dyn, %s_pack_dyn, %s_unpack_dyn, NULL, 0 },\n",
                    name, name, name, name, name);
        }
    }
    fprintf(f, "};\n\n");
    fprintf(f, "static int mpj_valid_type_id(int type_id) {\n    return type_id >= 0 && (size_t)type_id < mpj_type_count();\n}\n\n");
    fprintf(f, "static mpj_buffer *mpj_buffer_from_sbuffer(const msgpack_sbuffer *sbuf) {\n");
    fprintf(f, "    mpj_buffer *buffer = (mpj_buffer *)calloc(1, sizeof(*buffer));\n");
    fprintf(f, "    if (!buffer) return NULL;\n");
    fprintf(f, "    if (sbuf->size == 0) return buffer;\n");
    fprintf(f, "    buffer->data = (uint8_t *)malloc(sbuf->size);\n");
    fprintf(f, "    if (!buffer->data) { free(buffer); return NULL; }\n");
    fprintf(f, "    memcpy(buffer->data, sbuf->data, sbuf->size);\n");
    fprintf(f, "    buffer->size = sbuf->size;\n");
    fprintf(f, "    return buffer;\n}\n\n");
    fprintf(f, "const uint8_t *mpj_buffer_data(const mpj_buffer *buffer) {\n    return buffer ? buffer->data : NULL;\n}\n\n");
    fprintf(f, "size_t mpj_buffer_size(const mpj_buffer *buffer) {\n    return buffer ? buffer->size : 0;\n}\n\n");
    fprintf(f, "void mpj_buffer_delete(mpj_buffer *buffer) {\n    if (!buffer) return;\n    free(buffer->data);\n    free(buffer);\n}\n\n");
    fprintf(f, "struct mpj_value {\n    int kind;\n    union {\n        int boolean;\n        int64_t i64;\n        uint64_t u64;\n        double f64;\n        struct { uint8_t *data; size_t size; } bytes;\n        struct { mpj_value **items; size_t size; } array;\n        struct { mpj_value **keys; mpj_value **values; size_t size; } map;\n    } as;\n};\n\n");
    fprintf(f, "static mpj_value *mpj_value_alloc(int kind) {\n    mpj_value *value = (mpj_value *)calloc(1, sizeof(*value));\n    if (value) value->kind = kind;\n    return value;\n}\n\n");
    fprintf(f, "mpj_value *mpj_value_new_nil(void) { return mpj_value_alloc(MPJ_VALUE_NIL); }\n\n");
    fprintf(f, "mpj_value *mpj_value_new_bool(int value) {\n    mpj_value *out = mpj_value_alloc(MPJ_VALUE_BOOL);\n    if (out) out->as.boolean = value ? 1 : 0;\n    return out;\n}\n\n");
    fprintf(f, "mpj_value *mpj_value_new_int(int64_t value) {\n    mpj_value *out = mpj_value_alloc(MPJ_VALUE_INT);\n    if (out) out->as.i64 = value;\n    return out;\n}\n\n");
    fprintf(f, "mpj_value *mpj_value_new_uint(uint64_t value) {\n    mpj_value *out = mpj_value_alloc(MPJ_VALUE_UINT);\n    if (out) out->as.u64 = value;\n    return out;\n}\n\n");
    fprintf(f, "mpj_value *mpj_value_new_float(double value) {\n    mpj_value *out = mpj_value_alloc(MPJ_VALUE_FLOAT);\n    if (out) out->as.f64 = value;\n    return out;\n}\n\n");
    fprintf(f, "mpj_value *mpj_value_new_number(double value) {\n    if (value >= 0.0 && value <= 9007199254740991.0) {\n        uint64_t n = (uint64_t)value;\n        if ((double)n == value) return mpj_value_new_uint(n);\n    }\n    if (value >= -9007199254740991.0 && value <= 9007199254740991.0) {\n        int64_t n = (int64_t)value;\n        if ((double)n == value) return mpj_value_new_int(n);\n    }\n    return mpj_value_new_float(value);\n}\n\n");
    fprintf(f, "static mpj_value *mpj_value_new_bytes(int kind, const uint8_t *data, size_t size) {\n    mpj_value *out = mpj_value_alloc(kind);\n    if (!out) return NULL;\n    out->as.bytes.size = size;\n    if (size == 0) return out;\n    out->as.bytes.data = (uint8_t *)malloc(size);\n    if (!out->as.bytes.data) { free(out); return NULL; }\n    memcpy(out->as.bytes.data, data, size);\n    return out;\n}\n\n");
    fprintf(f, "mpj_value *mpj_value_new_string(const uint8_t *data, size_t size) { return mpj_value_new_bytes(MPJ_VALUE_STRING, data, size); }\n");
    fprintf(f, "mpj_value *mpj_value_new_binary(const uint8_t *data, size_t size) { return mpj_value_new_bytes(MPJ_VALUE_BINARY, data, size); }\n\n");
    fprintf(f, "mpj_value *mpj_value_new_array(size_t size) {\n    mpj_value *out = mpj_value_alloc(MPJ_VALUE_ARRAY);\n    if (!out) return NULL;\n    out->as.array.size = size;\n    if (size == 0) return out;\n    out->as.array.items = (mpj_value **)calloc(size, sizeof(mpj_value *));\n    if (!out->as.array.items) { free(out); return NULL; }\n    return out;\n}\n\n");
    fprintf(f, "mpj_value *mpj_value_new_map(size_t size) {\n    mpj_value *out = mpj_value_alloc(MPJ_VALUE_MAP);\n    if (!out) return NULL;\n    out->as.map.size = size;\n    if (size == 0) return out;\n    out->as.map.keys = (mpj_value **)calloc(size, sizeof(mpj_value *));\n    out->as.map.values = (mpj_value **)calloc(size, sizeof(mpj_value *));\n    if (!out->as.map.keys || !out->as.map.values) { free(out->as.map.keys); free(out->as.map.values); free(out); return NULL; }\n    return out;\n}\n\n");
    fprintf(f, "void mpj_value_free(mpj_value *value) {\n    if (!value) return;\n    if (value->kind == MPJ_VALUE_STRING || value->kind == MPJ_VALUE_BINARY) {\n        free(value->as.bytes.data);\n    } else if (value->kind == MPJ_VALUE_ARRAY) {\n        for (size_t i = 0; i < value->as.array.size; ++i) mpj_value_free(value->as.array.items[i]);\n        free(value->as.array.items);\n    } else if (value->kind == MPJ_VALUE_MAP) {\n        for (size_t i = 0; i < value->as.map.size; ++i) { mpj_value_free(value->as.map.keys[i]); mpj_value_free(value->as.map.values[i]); }\n        free(value->as.map.keys);\n        free(value->as.map.values);\n    }\n    free(value);\n}\n\n");
    fprintf(f, "int mpj_value_array_set(mpj_value *array, size_t index, mpj_value *item) {\n    if (!array || array->kind != MPJ_VALUE_ARRAY || index >= array->as.array.size) return -1;\n    mpj_value_free(array->as.array.items[index]);\n    array->as.array.items[index] = item;\n    return 0;\n}\n\n");
    fprintf(f, "int mpj_value_map_set(mpj_value *map, size_t index, mpj_value *key, mpj_value *value) {\n    if (!map || map->kind != MPJ_VALUE_MAP || index >= map->as.map.size) return -1;\n    mpj_value_free(map->as.map.keys[index]);\n    mpj_value_free(map->as.map.values[index]);\n    map->as.map.keys[index] = key;\n    map->as.map.values[index] = value;\n    return 0;\n}\n\n");
    fprintf(f, "static int mpj_pack_value(msgpack_packer *pk, const mpj_value *value) {\n    if (!pk) return -1;\n    if (!value) return msgpack_pack_nil(pk);\n    switch (value->kind) {\n    case MPJ_VALUE_NIL: return msgpack_pack_nil(pk);\n    case MPJ_VALUE_BOOL: return value->as.boolean ? msgpack_pack_true(pk) : msgpack_pack_false(pk);\n    case MPJ_VALUE_INT: return msgpack_pack_int64(pk, value->as.i64);\n    case MPJ_VALUE_UINT: return msgpack_pack_uint64(pk, value->as.u64);\n    case MPJ_VALUE_FLOAT: return msgpack_pack_double(pk, value->as.f64);\n    case MPJ_VALUE_STRING:\n        if (msgpack_pack_str(pk, value->as.bytes.size) != 0) return -1;\n        return msgpack_pack_str_body(pk, (const char *)value->as.bytes.data, value->as.bytes.size);\n    case MPJ_VALUE_BINARY:\n        if (msgpack_pack_bin(pk, value->as.bytes.size) != 0) return -1;\n        return msgpack_pack_bin_body(pk, value->as.bytes.data, value->as.bytes.size);\n    case MPJ_VALUE_ARRAY:\n        if (msgpack_pack_array(pk, value->as.array.size) != 0) return -1;\n        for (size_t i = 0; i < value->as.array.size; ++i) if (mpj_pack_value(pk, value->as.array.items[i]) != 0) return -1;\n        return 0;\n    case MPJ_VALUE_MAP:\n        if (msgpack_pack_map(pk, value->as.map.size) != 0) return -1;\n        for (size_t i = 0; i < value->as.map.size; ++i) {\n            if (mpj_pack_value(pk, value->as.map.keys[i]) != 0) return -1;\n            if (mpj_pack_value(pk, value->as.map.values[i]) != 0) return -1;\n        }\n        return 0;\n    default: return -1;\n    }\n}\n\n");
    fprintf(f, "mpj_buffer *mpj_value_pack_bytes(const mpj_value *value) {\n    if (!value) return NULL;\n    msgpack_sbuffer sbuf;\n    msgpack_packer pk;\n    msgpack_sbuffer_init(&sbuf);\n    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);\n    if (mpj_pack_value(&pk, value) != 0) { msgpack_sbuffer_destroy(&sbuf); return NULL; }\n    mpj_buffer *buffer = mpj_buffer_from_sbuffer(&sbuf);\n    msgpack_sbuffer_destroy(&sbuf);\n    return buffer;\n}\n\n");
    fprintf(f, "static mpj_value *mpj_value_from_object(const msgpack_object *obj) {\n    if (!obj) return NULL;\n    switch (obj->type) {\n    case MSGPACK_OBJECT_NIL: return mpj_value_new_nil();\n    case MSGPACK_OBJECT_BOOLEAN: return mpj_value_new_bool(obj->via.boolean);\n    case MSGPACK_OBJECT_POSITIVE_INTEGER: return mpj_value_new_uint(obj->via.u64);\n    case MSGPACK_OBJECT_NEGATIVE_INTEGER: return mpj_value_new_int(obj->via.i64);\n    case MSGPACK_OBJECT_FLOAT32:\n    case MSGPACK_OBJECT_FLOAT64: return mpj_value_new_float(obj->via.f64);\n    case MSGPACK_OBJECT_STR: return mpj_value_new_string((const uint8_t *)obj->via.str.ptr, obj->via.str.size);\n    case MSGPACK_OBJECT_BIN: return mpj_value_new_binary((const uint8_t *)obj->via.bin.ptr, obj->via.bin.size);\n    case MSGPACK_OBJECT_ARRAY: {\n        mpj_value *out = mpj_value_new_array(obj->via.array.size);\n        if (!out) return NULL;\n        for (uint32_t i = 0; i < obj->via.array.size; ++i) mpj_value_array_set(out, i, mpj_value_from_object(&obj->via.array.ptr[i]));\n        return out;\n    }\n    case MSGPACK_OBJECT_MAP: {\n        mpj_value *out = mpj_value_new_map(obj->via.map.size);\n        if (!out) return NULL;\n        for (uint32_t i = 0; i < obj->via.map.size; ++i) mpj_value_map_set(out, i, mpj_value_from_object(&obj->via.map.ptr[i].key), mpj_value_from_object(&obj->via.map.ptr[i].val));\n        return out;\n    }\n    default: return NULL;\n    }\n}\n\n");
    fprintf(f, "mpj_value *mpj_value_unpack_bytes(const uint8_t *data, size_t size) {\n    if (!data && size > 0) return NULL;\n    msgpack_unpacked result;\n    msgpack_unpacked_init(&result);\n    size_t off = 0;\n    msgpack_unpack_return ret = msgpack_unpack_next(&result, (const char *)data, size, &off);\n    if (ret != MSGPACK_UNPACK_SUCCESS) { msgpack_unpacked_destroy(&result); return NULL; }\n    mpj_value *out = mpj_value_from_object(&result.data);\n    msgpack_unpacked_destroy(&result);\n    return out;\n}\n\n");
    fprintf(f, "int mpj_value_kind(const mpj_value *value) { return value ? value->kind : -1; }\n");
    fprintf(f, "int mpj_value_bool(const mpj_value *value) { return value && value->kind == MPJ_VALUE_BOOL ? value->as.boolean : 0; }\n");
    fprintf(f, "int64_t mpj_value_int(const mpj_value *value) { return value && value->kind == MPJ_VALUE_INT ? value->as.i64 : 0; }\n");
    fprintf(f, "uint64_t mpj_value_uint(const mpj_value *value) { return value && value->kind == MPJ_VALUE_UINT ? value->as.u64 : 0; }\n");
    fprintf(f, "double mpj_value_float(const mpj_value *value) { return value && value->kind == MPJ_VALUE_FLOAT ? value->as.f64 : 0.0; }\n");
    fprintf(f, "double mpj_value_number(const mpj_value *value) {\n    if (!value) return 0.0;\n    if (value->kind == MPJ_VALUE_INT) return (double)value->as.i64;\n    if (value->kind == MPJ_VALUE_UINT) return (double)value->as.u64;\n    if (value->kind == MPJ_VALUE_FLOAT) return value->as.f64;\n    return 0.0;\n}\n");
    fprintf(f, "const uint8_t *mpj_value_data(const mpj_value *value) {\n    if (!value || (value->kind != MPJ_VALUE_STRING && value->kind != MPJ_VALUE_BINARY)) return NULL;\n    return value->as.bytes.data;\n}\n");
    fprintf(f, "size_t mpj_value_size(const mpj_value *value) {\n    if (!value) return 0;\n    if (value->kind == MPJ_VALUE_STRING || value->kind == MPJ_VALUE_BINARY) return value->as.bytes.size;\n    if (value->kind == MPJ_VALUE_ARRAY) return value->as.array.size;\n    if (value->kind == MPJ_VALUE_MAP) return value->as.map.size;\n    return 0;\n}\n");
    fprintf(f, "const mpj_value *mpj_value_array_get(const mpj_value *array, size_t index) {\n    if (!array || array->kind != MPJ_VALUE_ARRAY || index >= array->as.array.size) return NULL;\n    return array->as.array.items[index];\n}\n");
    fprintf(f, "const mpj_value *mpj_value_map_key(const mpj_value *map, size_t index) {\n    if (!map || map->kind != MPJ_VALUE_MAP || index >= map->as.map.size) return NULL;\n    return map->as.map.keys[index];\n}\n");
    fprintf(f, "const mpj_value *mpj_value_map_value(const mpj_value *map, size_t index) {\n    if (!map || map->kind != MPJ_VALUE_MAP || index >= map->as.map.size) return NULL;\n    return map->as.map.values[index];\n}\n\n");
    fprintf(f, "size_t mpj_type_count(void) {\n    return sizeof(mpj_types) / sizeof(mpj_types[0]);\n}\n\n");
    fprintf(f, "const char *mpj_type_name(size_t type_id) {\n    if (type_id >= mpj_type_count()) return NULL;\n    return mpj_types[type_id].name;\n}\n\n");
    fprintf(f, "int mpj_type_id(const char *type_name) {\n    if (!type_name) return -1;\n    for (size_t i = 0; i < mpj_type_count(); ++i) {\n        if (strcmp(type_name, mpj_types[i].name) == 0) return (int)i;\n    }\n    return -1;\n}\n\n");
    fprintf(f, "static const mpj_field_entry *mpj_field_at(int type_id, size_t field_id) {\n");
    fprintf(f, "    if (!mpj_valid_type_id(type_id)) return NULL;\n");
    fprintf(f, "    if (field_id >= mpj_types[type_id].field_count) return NULL;\n");
    fprintf(f, "    return &mpj_types[type_id].fields[field_id];\n");
    fprintf(f, "}\n\n");
    fprintf(f, "size_t mpj_field_count(int type_id) {\n    if (!mpj_valid_type_id(type_id)) return 0;\n    return mpj_types[type_id].field_count;\n}\n\n");
    fprintf(f, "const char *mpj_field_name(int type_id, size_t field_id) {\n    const mpj_field_entry *field = mpj_field_at(type_id, field_id);\n    return field ? field->name : NULL;\n}\n\n");
    fprintf(f, "const char *mpj_field_c_type(int type_id, size_t field_id) {\n    const mpj_field_entry *field = mpj_field_at(type_id, field_id);\n    return field ? field->c_type : NULL;\n}\n\n");
    fprintf(f, "int mpj_field_key_kind(int type_id, size_t field_id) {\n    const mpj_field_entry *field = mpj_field_at(type_id, field_id);\n    return field ? field->key_kind : -1;\n}\n\n");
    fprintf(f, "const char *mpj_field_key_string(int type_id, size_t field_id) {\n    const mpj_field_entry *field = mpj_field_at(type_id, field_id);\n    return field ? field->key_string : NULL;\n}\n\n");
    fprintf(f, "int64_t mpj_field_key_int(int type_id, size_t field_id) {\n    const mpj_field_entry *field = mpj_field_at(type_id, field_id);\n    return field ? field->key_int : 0;\n}\n\n");
    fprintf(f, "void *mpj_new(int type_id) {\n    if (!mpj_valid_type_id(type_id)) return NULL;\n    return calloc(1, mpj_types[type_id].size);\n}\n\n");
    fprintf(f, "int mpj_delete(int type_id, void *value) {\n    if (!mpj_valid_type_id(type_id)) return -1;\n    if (!value) return 0;\n    mpj_types[type_id].free_fn(value);\n    free(value);\n    return 0;\n}\n\n");
    fprintf(f, "mpj_buffer *mpj_pack_bytes(int type_id, const void *value) {\n");
    fprintf(f, "    if (!mpj_valid_type_id(type_id) || !value) return NULL;\n    msgpack_sbuffer sbuf;\n    msgpack_packer pk;\n    msgpack_sbuffer_init(&sbuf);\n    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);\n");
    fprintf(f, "    if (mpj_types[type_id].pack_fn(&pk, value) != 0) { msgpack_sbuffer_destroy(&sbuf); return NULL; }\n");
    fprintf(f, "    mpj_buffer *buffer = mpj_buffer_from_sbuffer(&sbuf);\n    msgpack_sbuffer_destroy(&sbuf);\n    return buffer;\n}\n\n");
    fprintf(f, "int mpj_unpack_bytes(int type_id, const uint8_t *data, size_t size, void *out) {\n");
    fprintf(f, "    if (!mpj_valid_type_id(type_id) || !data || !out) return -1;\n    msgpack_unpacked result;\n    msgpack_unpacked_init(&result);\n    size_t off = 0;\n");
    fprintf(f, "    msgpack_unpack_return ret = msgpack_unpack_next(&result, (const char *)data, size, &off);\n");
    fprintf(f, "    if (ret != MSGPACK_UNPACK_SUCCESS) { msgpack_unpacked_destroy(&result); return -1; }\n");
    fprintf(f, "    int rc = mpj_types[type_id].unpack_fn(&result.data, out);\n    msgpack_unpacked_destroy(&result);\n    return rc;\n}\n");
}

static void generate_js_wrapper(FILE *f, const Model *m) {
    fprintf(f, "let wasmModule = null;\n\n");
    fprintf(f, "export function useMsgpackPjsekaiWasm(Module) {\n  wasmModule = Module;\n  return Module;\n}\n\n");
    fprintf(f, "function requireWasm() {\n  if (!wasmModule) throw new Error('call useMsgpackPjsekaiWasm(Module) before encode/decode');\n  return wasmModule;\n}\n\n");
    fprintf(f, "export const schemas = {\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        fprintf(f, "  %s: [\n", c->c_name);
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "    { name: ");
            fprint_json_string(f, mem->c_name);
            fprintf(f, ", key: ");
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "%ld", mem->key_int);
            fprintf(f, ", keyKind: ");
            fprint_json_string(f, mem->key_kind == KEY_INT ? "int" : "string");
            fprintf(f, " },\n");
        }
        fprintf(f, "  ],\n");
    }
    fprintf(f, "};\n\n");
    fprintf(f, "export const types = () => Object.keys(schemas);\n");
    fprintf(f, "export const fields = (typeName) => schemas[typeName] || [];\n\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        char *name = export_ident(c->c_name);
        fprintf(f, "export class %s {\n", name);
        fprintf(f, "  static schema = schemas.%s;\n", c->c_name);
        fprintf(f, "  constructor(init = {}) {\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "    this.%s = init.%s;\n", mem->c_name, mem->c_name);
        }
        fprintf(f, "  }\n\n");
        fprintf(f, "  toMsgpackMap() {\n    const out = new Map();\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "    if (this.%s !== undefined) out.set(", mem->c_name);
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "%ld", mem->key_int);
            fprintf(f, ", this.%s);\n", mem->c_name);
        }
        fprintf(f, "    return out;\n  }\n\n");
        fprintf(f, "  encode() {\n    const out = requireWasm().%s_encode(this);\n    if (!(out instanceof Uint8Array)) throw new Error('MessagePack encode failed');\n    return out;\n  }\n\n", c->c_name);
        fprintf(f, "  decode(bytes) {\n    const input = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);\n    const raw = requireWasm().%s_decode(input);\n", c->c_name);
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "    if (Object.prototype.hasOwnProperty.call(raw, ");
            fprint_json_string(f, mem->c_name);
            fprintf(f, ")) this.%s = raw.%s;\n", mem->c_name, mem->c_name);
        }
        fprintf(f, "    return this;\n  }\n}\n\n");
        free(name);
    }
}








static void generate_js_package_json(FILE *f) {
    fprintf(f, "{\n");
    fprintf(f, "  \"name\": \"%s\",\n", JS_PACKAGE_NAME);
    fprintf(f, "  \"version\": \"%s\",\n", PACKAGE_VERSION);
    fprintf(f, "  \"description\": \"Static object-based MessagePack models generated from dump.cs.\",\n");
    fprintf(f, "  \"type\": \"module\",\n");
    fprintf(f, "  \"license\": \"MIT\",\n");
    fprintf(f, "  \"homepage\": \"%s#readme\",\n", PROJECT_URL);
    fprintf(f, "  \"repository\": { \"type\": \"git\", \"url\": \"git+%s.git\", \"directory\": \"msgpack/wrappers/js\" },\n", PROJECT_URL);
    fprintf(f, "  \"bugs\": { \"url\": \"%s/issues\" },\n", PROJECT_URL);
    fprintf(f, "  \"module\": \"./msgpack-pjsekai.js\",\n");
    fprintf(f, "  \"types\": \"./msgpack-pjsekai.d.ts\",\n");
    fprintf(f, "  \"exports\": {\n");
    fprintf(f, "    \".\": { \"types\": \"./msgpack-pjsekai.d.ts\", \"default\": \"./msgpack-pjsekai.js\" },\n");
    fprintf(f, "    \"./wasm\": { \"types\": \"./msgpack-pjsekai-wasm.d.ts\", \"default\": \"./msgpack-pjsekai-wasm.js\" },\n");
    fprintf(f, "    \"./msgpack-pjsekai-wasm.wasm\": \"./msgpack-pjsekai-wasm.wasm\"\n");
    fprintf(f, "  },\n");
    fprintf(f, "  \"files\": [\n");
    fprintf(f, "    \"msgpack-pjsekai.js\",\n");
    fprintf(f, "    \"msgpack-pjsekai.d.ts\",\n");
    fprintf(f, "    \"msgpack-pjsekai-wasm.js\",\n");
    fprintf(f, "    \"msgpack-pjsekai-wasm.wasm\",\n");
    fprintf(f, "    \"msgpack-pjsekai-wasm.d.ts\",\n");
    fprintf(f, "    \"README.md\"\n");
    fprintf(f, "  ],\n");
    fprintf(f, "  \"scripts\": {\n");
    fprintf(f, "    \"build:wasm\": \"sh ./build-wasm.sh\",\n");
    fprintf(f, "    \"check\": \"node --check msgpack-pjsekai.js && (test ! -f msgpack-pjsekai-wasm.js || node --check msgpack-pjsekai-wasm.js)\",\n");
    fprintf(f, "    \"pack:dry-run\": \"npm pack --dry-run\",\n");
    fprintf(f, "    \"prepack\": \"npm run build:wasm\"\n");
    fprintf(f, "  },\n");
    fprintf(f, "  \"dependencies\": {},\n");
    fprintf(f, "  \"keywords\": [\"msgpack\", \"pjsekai\", \"generated\", \"protobuf-like\"],\n");
    fprintf(f, "  \"engines\": { \"node\": \">=18\" }\n");
    fprintf(f, "}\n");
}


static void generate_js_readme(FILE *f) {
    fprintf(f, "# JavaScript object wrapper\n\n");
    fprintf(f, "This directory is an npm package named `%s`. It does not depend on a JavaScript MessagePack package; encode/decode calls the bundled generated WASM bridge.\n\n", JS_PACKAGE_NAME);
    fprintf(f, "The npm package includes `msgpack-pjsekai-wasm.js` and `msgpack-pjsekai-wasm.wasm`, built from the generated C sources with Emscripten before packing or publishing.\n\n");
    fprintf(f, "Install from npm after a release is published:\n\n");
    fprintf(f, "```sh\nnpm install %s\n```\n\n", JS_PACKAGE_NAME);
    fprintf(f, "```sh\ncd wrappers/js\nnpm install\nnpm run build:wasm\nnpm run check\nnpm pack --dry-run\n```\n\n");
    fprintf(f, "Publish through the repository `Publish Packages` GitHub Actions workflow, or manually with `npm publish --access public --provenance` after npm trusted publishing is configured.\n\n");
    fprintf(f, "`msgpack-pjsekai.d.ts` contains one generated class per MessagePack struct, so editors can show the available fields.\n\n");
    fprintf(f, "```js\nimport createModule from '%s/wasm';\nimport { useMsgpackPjsekaiWasm, Sekai_AssetBundleElement } from '%s';\n\nuseMsgpackPjsekaiWasm(await createModule());\n\nconst value = new Sekai_AssetBundleElement({\n  bundleName: 'example',\n  crc: 1234,\n});\n\nconst bytes = value.encode(); // Uint8Array\nconst out = new Sekai_AssetBundleElement().decode(bytes);\nconsole.log(out.bundleName);\n```\n", JS_PACKAGE_NAME, JS_PACKAGE_NAME);
}


static void generate_js_types(FILE *f, const Model *m) {
    fprintf(f, "export type MsgpackBuffer = Uint8Array;\n");
    fprintf(f, "export type MsgpackPjsekaiWasmModule = {\n  [typeName: string]: unknown;\n};\n\n");
    fprintf(f, "export declare function useMsgpackPjsekaiWasm(Module: MsgpackPjsekaiWasmModule): MsgpackPjsekaiWasmModule;\n\n");
    fprintf(f, "export interface FieldInfo {\n  name: string;\n  key: string | number;\n  keyKind: 'string' | 'int';\n}\n\n");
    fprintf(f, "export declare const schemas: {\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        fprintf(f, "  %s: readonly FieldInfo[];\n", c->c_name);
    }
    fprintf(f, "};\n\n");
    fprintf(f, "export type MsgpackPjsekaiTypeName =\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        fprintf(f, "  | ");
        fprint_json_string(f, m->classes[i].c_name);
        fprintf(f, "\n");
    }
    fprintf(f, ";\n\n");
    fprintf(f, "export declare const types: () => MsgpackPjsekaiTypeName[];\n");
    fprintf(f, "export declare const fields: (typeName: MsgpackPjsekaiTypeName | string) => readonly FieldInfo[];\n\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        char *name = export_ident(c->c_name);
        fprintf(f, "export interface %sInit {\n", name);
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "  ");
            fprint_json_string(f, mem->c_name);
            fprintf(f, "?: %s;\n", ts_type_for(mem->cs_type));
        }
        fprintf(f, "}\n\n");
        fprintf(f, "export declare class %s {\n", name);
        fprintf(f, "  static schema: readonly FieldInfo[];\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "  ");
            fprint_json_string(f, mem->c_name);
            fprintf(f, "?: %s;\n", ts_type_for(mem->cs_type));
        }
        fprintf(f, "  constructor(init?: %sInit);\n", name);
        fprintf(f, "  toMsgpackMap(): Map<string | number, unknown>;\n");
        fprintf(f, "  encode(): MsgpackBuffer;\n");
        fprintf(f, "  decode(bytes: ArrayLike<number>): this;\n");
        fprintf(f, "}\n\n");
        free(name);
    }
}


static void generate_js_wasm_types(FILE *f) {
    fputs("import type { MsgpackPjsekaiWasmModule } from './msgpack-pjsekai.js';\n\n", f);
    fputs("export interface MsgpackPjsekaiWasmFactoryOptions {\n", f);
    fputs("  locateFile?(path: string, prefix: string): string;\n", f);
    fputs("  wasmBinary?: ArrayBuffer | Uint8Array;\n", f);
    fputs("  [key: string]: unknown;\n", f);
    fputs("}\n\n", f);
    fputs("export default function createMsgpackPjsekaiWasm(\n", f);
    fputs("  options?: MsgpackPjsekaiWasmFactoryOptions\n", f);
    fputs("): Promise<MsgpackPjsekaiWasmModule>;\n", f);
}


static void generate_js_wasm_build_script(FILE *f) {
    fputs("#!/usr/bin/env sh\n", f);
    fputs("set -eu\n\n", f);
    fputs("if ! command -v emcc >/dev/null 2>&1; then\n", f);
    fputs("  echo \"emcc not found; install and activate Emscripten SDK before building the WASM package\" >&2\n", f);
    fputs("  exit 127\n", f);
    fputs("fi\n\n", f);
    fputs("SCRIPT_DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n", f);
    fputs("ROOT=$(CDPATH= cd -- \"$SCRIPT_DIR/../..\" && pwd)\n", f);
    fputs("OUT_JS=${1:-\"$SCRIPT_DIR/msgpack-pjsekai-wasm.js\"}\n", f);
    fputs("TMP_DIR=$(mktemp -d)\n", f);
    fputs("trap 'rm -rf \"$TMP_DIR\"' EXIT INT TERM\n\n", f);
    fputs("mkdir -p \"$TMP_DIR/include/msgpack\"\n", f);
    fputs("sed -e 's/@MSGPACK_ENDIAN_BIG_BYTE@/0/g' \\\n", f);
    fputs("    -e 's/@MSGPACK_ENDIAN_LITTLE_BYTE@/1/g' \\\n", f);
    fputs("    \"$ROOT/deps/cmake/sysdep.h.in\" > \"$TMP_DIR/include/msgpack/sysdep.h\"\n", f);
    fputs("cp \"$TMP_DIR/include/msgpack/sysdep.h\" \"$TMP_DIR/include/sysdep.h\"\n", f);
    fputs("sed -e 's/@MSGPACK_ENDIAN_BIG_BYTE@/0/g' \\\n", f);
    fputs("    -e 's/@MSGPACK_ENDIAN_LITTLE_BYTE@/1/g' \\\n", f);
    fputs("    \"$ROOT/deps/cmake/pack_template.h.in\" > \"$TMP_DIR/include/msgpack/pack_template.h\"\n", f);
    fputs("cp \"$TMP_DIR/include/msgpack/pack_template.h\" \"$TMP_DIR/include/pack_template.h\"\n\n", f);
    fputs("SOURCES=$(awk -v root=\"$ROOT\" '\n", f);
    fputs("  /^[[:space:]]*($|#|\\*)/ { next }\n", f);
    fputs("  { print root \"/\" $0 }\n", f);
    fputs("' \"$ROOT/msgpack-pjsekai.files\")\n\n", f);
    fputs("emcc \\\n", f);
    fputs("  $SOURCES \\\n", f);
    fputs("  \"$SCRIPT_DIR/msgpack-pjsekai-wasm-bridge.cpp\" \\\n", f);
    fputs("  \"$ROOT/deps/src/objectc.c\" \\\n", f);
    fputs("  \"$ROOT/deps/src/unpack.c\" \\\n", f);
    fputs("  \"$ROOT/deps/src/version.c\" \\\n", f);
    fputs("  \"$ROOT/deps/src/vrefbuffer.c\" \\\n", f);
    fputs("  \"$ROOT/deps/src/zone.c\" \\\n", f);
    fputs("  -I\"$ROOT\" \\\n", f);
    fputs("  -I\"$ROOT/generated\" \\\n", f);
    fputs("  -I\"$TMP_DIR/include\" \\\n", f);
    fputs("  -I\"$ROOT/deps/include\" \\\n", f);
    fputs("  -O1 \\\n", f);
    fputs("  --bind \\\n", f);
    fputs("  -sMODULARIZE=1 \\\n", f);
    fputs("  -sEXPORT_ES6=1 \\\n", f);
    fputs("  -sENVIRONMENT=web,node \\\n", f);
    fputs("  -sALLOW_MEMORY_GROWTH=1 \\\n", f);
    fputs("  -o \"$OUT_JS\"\n", f);
}

static void generate_python_wrapper(FILE *f, const Model *m) {
    fprintf(f, "from __future__ import annotations\n\n");
    fprintf(f, "from collections.abc import Mapping\nfrom dataclasses import dataclass\nfrom typing import Any, ClassVar, Dict, List, Optional\n\nimport ctypes\nimport os\n\n");
    fprintf(f, "FieldInfo = Dict[str, Any]\nMsgpackMap = Dict[Any, Any]\n\n");
    fprintf(f, "class _MpjBuffer(ctypes.Structure):\n    _fields_ = [('data', ctypes.POINTER(ctypes.c_uint8)), ('size', ctypes.c_size_t)]\n\n");
    fprintf(f, "class _Backend:\n    def __init__(self, library_path: str):\n        self.lib = ctypes.CDLL(library_path)\n        self.lib.mpj_buffer_data.restype = ctypes.POINTER(ctypes.c_uint8)\n        self.lib.mpj_buffer_data.argtypes = [ctypes.POINTER(_MpjBuffer)]\n        self.lib.mpj_buffer_size.restype = ctypes.c_size_t\n        self.lib.mpj_buffer_size.argtypes = [ctypes.POINTER(_MpjBuffer)]\n        self.lib.mpj_buffer_delete.argtypes = [ctypes.POINTER(_MpjBuffer)]\n        self.lib.mpj_value_new_nil.restype = ctypes.c_void_p\n        self.lib.mpj_value_new_bool.restype = ctypes.c_void_p\n        self.lib.mpj_value_new_bool.argtypes = [ctypes.c_int]\n        self.lib.mpj_value_new_int.restype = ctypes.c_void_p\n        self.lib.mpj_value_new_int.argtypes = [ctypes.c_int64]\n        self.lib.mpj_value_new_uint.restype = ctypes.c_void_p\n        self.lib.mpj_value_new_uint.argtypes = [ctypes.c_uint64]\n        self.lib.mpj_value_new_float.restype = ctypes.c_void_p\n        self.lib.mpj_value_new_float.argtypes = [ctypes.c_double]\n        self.lib.mpj_value_new_string.restype = ctypes.c_void_p\n        self.lib.mpj_value_new_string.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]\n        self.lib.mpj_value_new_binary.restype = ctypes.c_void_p\n        self.lib.mpj_value_new_binary.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]\n        self.lib.mpj_value_new_array.restype = ctypes.c_void_p\n        self.lib.mpj_value_new_array.argtypes = [ctypes.c_size_t]\n        self.lib.mpj_value_new_map.restype = ctypes.c_void_p\n        self.lib.mpj_value_new_map.argtypes = [ctypes.c_size_t]\n        self.lib.mpj_value_free.argtypes = [ctypes.c_void_p]\n        self.lib.mpj_value_array_set.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]\n        self.lib.mpj_value_map_set.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p]\n        self.lib.mpj_value_pack_bytes.restype = ctypes.POINTER(_MpjBuffer)\n        self.lib.mpj_value_pack_bytes.argtypes = [ctypes.c_void_p]\n        self.lib.mpj_value_unpack_bytes.restype = ctypes.c_void_p\n        self.lib.mpj_value_unpack_bytes.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]\n        self.lib.mpj_value_kind.restype = ctypes.c_int\n        self.lib.mpj_value_kind.argtypes = [ctypes.c_void_p]\n        self.lib.mpj_value_bool.restype = ctypes.c_int\n        self.lib.mpj_value_bool.argtypes = [ctypes.c_void_p]\n        self.lib.mpj_value_int.restype = ctypes.c_int64\n        self.lib.mpj_value_int.argtypes = [ctypes.c_void_p]\n        self.lib.mpj_value_uint.restype = ctypes.c_uint64\n        self.lib.mpj_value_uint.argtypes = [ctypes.c_void_p]\n        self.lib.mpj_value_float.restype = ctypes.c_double\n        self.lib.mpj_value_float.argtypes = [ctypes.c_void_p]\n        self.lib.mpj_value_data.restype = ctypes.POINTER(ctypes.c_uint8)\n        self.lib.mpj_value_data.argtypes = [ctypes.c_void_p]\n        self.lib.mpj_value_size.restype = ctypes.c_size_t\n        self.lib.mpj_value_size.argtypes = [ctypes.c_void_p]\n        self.lib.mpj_value_array_get.restype = ctypes.c_void_p\n        self.lib.mpj_value_array_get.argtypes = [ctypes.c_void_p, ctypes.c_size_t]\n        self.lib.mpj_value_map_key.restype = ctypes.c_void_p\n        self.lib.mpj_value_map_key.argtypes = [ctypes.c_void_p, ctypes.c_size_t]\n        self.lib.mpj_value_map_value.restype = ctypes.c_void_p\n        self.lib.mpj_value_map_value.argtypes = [ctypes.c_void_p, ctypes.c_size_t]\n\n_backend: Optional[_Backend] = None\n\n");
    fprintf(f, "def load_c_library(library_path: Optional[str] = None) -> _Backend:\n    global _backend\n    candidates: List[str] = []\n    if library_path:\n        candidates.append(library_path)\n    env_path = os.environ.get('MSGPACK_PJSEKAI_LIB')\n    if env_path:\n        candidates.append(env_path)\n    here = os.path.dirname(__file__)\n    candidates.extend([\n        os.path.join(here, 'libmsgpack_pjsekai.dylib'),\n        os.path.join(here, 'libmsgpack_pjsekai.so'),\n        os.path.join(here, 'msgpack_pjsekai.dll'),\n        'libmsgpack_pjsekai.dylib',\n        'libmsgpack_pjsekai.so',\n        'msgpack_pjsekai.dll',\n    ])\n    errors: List[str] = []\n    for candidate in candidates:\n        try:\n            _backend = _Backend(candidate)\n            return _backend\n        except OSError as exc:\n            errors.append(f'{candidate}: {exc}')\n    raise RuntimeError('unable to load msgpack-pjsekai C library; set MSGPACK_PJSEKAI_LIB or call load_c_library(path)')\n\ndef _require_backend() -> _Backend:\n    return _backend if _backend is not None else load_c_library()\n\n");
    fprintf(f, "def _bytes_arg(data: bytes) -> Any:\n    return (ctypes.c_uint8 * len(data)).from_buffer_copy(data) if data else None\n\n");
    fprintf(f, "def _value_from_py(value: Any) -> int:\n    lib = _require_backend().lib\n    if value is None:\n        return int(lib.mpj_value_new_nil())\n    if isinstance(value, bool):\n        return int(lib.mpj_value_new_bool(1 if value else 0))\n    if isinstance(value, int):\n        if value >= 0:\n            return int(lib.mpj_value_new_uint(value))\n        return int(lib.mpj_value_new_int(value))\n    if isinstance(value, float):\n        return int(lib.mpj_value_new_float(value))\n    if isinstance(value, str):\n        data = value.encode('utf-8')\n        return int(lib.mpj_value_new_string(_bytes_arg(data), len(data)))\n    if isinstance(value, (bytes, bytearray, memoryview)):\n        data = bytes(value)\n        return int(lib.mpj_value_new_binary(_bytes_arg(data), len(data)))\n    if isinstance(value, (list, tuple)):\n        out = int(lib.mpj_value_new_array(len(value)))\n        for index, item in enumerate(value):\n            lib.mpj_value_array_set(out, index, _value_from_py(item))\n        return out\n    if isinstance(value, Mapping):\n        items = list(value.items())\n        out = int(lib.mpj_value_new_map(len(items)))\n        for index, (key, item) in enumerate(items):\n            lib.mpj_value_map_set(out, index, _value_from_py(key), _value_from_py(item))\n        return out\n    raise TypeError(f'unsupported MessagePack value: {type(value).__name__}')\n\n");
    fprintf(f, "def _py_from_value(ptr: int) -> Any:\n    lib = _require_backend().lib\n    kind = lib.mpj_value_kind(ptr)\n    if kind == 0:\n        return None\n    if kind == 1:\n        return bool(lib.mpj_value_bool(ptr))\n    if kind == 2:\n        return int(lib.mpj_value_int(ptr))\n    if kind == 3:\n        return int(lib.mpj_value_uint(ptr))\n    if kind == 4:\n        return float(lib.mpj_value_float(ptr))\n    if kind in (5, 6):\n        size = lib.mpj_value_size(ptr)\n        data = ctypes.string_at(lib.mpj_value_data(ptr), size)\n        return data.decode('utf-8') if kind == 5 else data\n    if kind == 7:\n        return [_py_from_value(lib.mpj_value_array_get(ptr, i)) for i in range(lib.mpj_value_size(ptr))]\n    if kind == 8:\n        return {_py_from_value(lib.mpj_value_map_key(ptr, i)): _py_from_value(lib.mpj_value_map_value(ptr, i)) for i in range(lib.mpj_value_size(ptr))}\n    return None\n\n");
    fprintf(f, "def _has_key(value: Mapping[Any, Any], key: Any) -> bool:\n    if key in value:\n        return True\n    if isinstance(key, int):\n        return any(isinstance(k, int) and k == key for k in value.keys())\n    return False\n\n");
    fprintf(f, "def _get_key(value: Mapping[Any, Any], key: Any) -> Any:\n    if key in value:\n        return value[key]\n    if isinstance(key, int):\n        for actual, item in value.items():\n            if isinstance(actual, int) and actual == key:\n                return item\n    return None\n\n");
    fprintf(f, "def _pack_map(value: Mapping[Any, Any]) -> bytes:\n    lib = _require_backend().lib\n    ptr = _value_from_py(value)\n    try:\n        buf = lib.mpj_value_pack_bytes(ptr)\n        if not buf:\n            raise RuntimeError('MessagePack encode failed')\n        try:\n            return ctypes.string_at(lib.mpj_buffer_data(buf), lib.mpj_buffer_size(buf))\n        finally:\n            lib.mpj_buffer_delete(buf)\n    finally:\n        lib.mpj_value_free(ptr)\n\n");
    fprintf(f, "def _unpack_map(data: bytes) -> Mapping[Any, Any]:\n    lib = _require_backend().lib\n    arr = _bytes_arg(data)\n    ptr = lib.mpj_value_unpack_bytes(arr, len(data))\n    if not ptr:\n        raise ValueError('MessagePack decode failed')\n    try:\n        value = _py_from_value(ptr)\n    finally:\n        lib.mpj_value_free(ptr)\n    if not isinstance(value, Mapping):\n        raise ValueError('expected a MessagePack map')\n    return value\n\n");
    fprintf(f, "schemas: Dict[str, List[FieldInfo]] = {\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        fprintf(f, "    ");
        fprint_json_string(f, c->c_name);
        fprintf(f, ": [\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "        {'name': ");
            fprint_json_string(f, mem->c_name);
            fprintf(f, ", 'key': ");
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "%ld", mem->key_int);
            fprintf(f, ", 'key_kind': ");
            fprint_json_string(f, mem->key_kind == KEY_INT ? "int" : "string");
            fprintf(f, "},\n");
        }
        fprintf(f, "    ],\n");
    }
    fprintf(f, "}\n\n");
    fprintf(f, "def types() -> List[str]:\n    return list(schemas.keys())\n\n");
    fprintf(f, "def fields(type_name: str) -> List[FieldInfo]:\n    return list(schemas[type_name])\n\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        fprintf(f, "@dataclass\nclass %s:\n", c->c_name);
        fprintf(f, "    __schema__: ClassVar[List[FieldInfo]] = schemas[");
        fprint_json_string(f, c->c_name);
        fprintf(f, "]\n");
        if (c->member_count == 0) {
            fprintf(f, "    pass\n\n");
        } else {
            for (size_t j = 0; j < c->member_count; ++j) {
                const Member *mem = &c->members[j];
                fprintf(f, "    %s: %s = None\n", mem->c_name, python_optional_type_for(mem->cs_type));
            }
            fprintf(f, "\n");
        }
        fprintf(f, "    def to_msgpack_map(self) -> MsgpackMap:\n        out: MsgpackMap = {}\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "        if self.%s is not None:\n            out[", mem->c_name);
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "%ld", mem->key_int);
            fprintf(f, "] = self.%s\n", mem->c_name);
        }
        fprintf(f, "        return out\n\n");
        fprintf(f, "    def encode(self) -> bytes:\n        return _pack_map(self.to_msgpack_map())\n\n");
        fprintf(f, "    def from_msgpack_map(self, value: Mapping[Any, Any]) -> '%s':\n", c->c_name);
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "        if _has_key(value, ");
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "%ld", mem->key_int);
            fprintf(f, "):\n            self.%s = _get_key(value, ", mem->c_name);
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "%ld", mem->key_int);
            fprintf(f, ")\n");
        }
        fprintf(f, "        return self\n\n");
        fprintf(f, "    def decode(self, data: bytes) -> '%s':\n        return self.from_msgpack_map(_unpack_map(data))\n\n", c->c_name);
    }
}


static void generate_python_pyproject(FILE *f) {
    fprintf(f, "[build-system]\n");
    fprintf(f, "requires = [\"setuptools>=77\", \"wheel\"]\n");
    fprintf(f, "build-backend = \"setuptools.build_meta\"\n\n");
    fprintf(f, "[project]\n");
    fprintf(f, "name = \"%s\"\n", PYTHON_PACKAGE_NAME);
    fprintf(f, "version = \"%s\"\n", PACKAGE_VERSION);
    fprintf(f, "description = \"Static object-based MessagePack models generated from dump.cs\"\n");
    fprintf(f, "readme = \"README.md\"\n");
    fprintf(f, "requires-python = \">=3.8\"\n");
    fprintf(f, "license = \"MIT\"\n");
    fprintf(f, "dependencies = []\n");
    fprintf(f, "keywords = [\"msgpack\", \"pjsekai\", \"generated\"]\n");
    fprintf(f, "classifiers = [\n");
    fprintf(f, "    \"Programming Language :: Python :: 3\",\n");
    fprintf(f, "    \"Programming Language :: Python :: 3 :: Only\"\n");
    fprintf(f, "]\n\n");
    fprintf(f, "[project.urls]\n");
    fprintf(f, "Homepage = \"%s\"\n", PROJECT_URL);
    fprintf(f, "Repository = \"%s\"\n", PROJECT_URL);
    fprintf(f, "Issues = \"%s/issues\"\n\n", PROJECT_URL);
    fprintf(f, "[tool.setuptools]\n");
    fprintf(f, "packages = [\"msgpack_pjsekai\"]\n\n");
    fprintf(f, "[tool.setuptools.package-data]\n");
    fprintf(f, "msgpack_pjsekai = [\"__init__.pyi\", \"py.typed\"]\n");
}


static void generate_python_manifest(FILE *f) {
    fprintf(f, "include msgpack_pjsekai/__init__.pyi\n");
    fprintf(f, "include msgpack_pjsekai/py.typed\n");
}

static void generate_python_py_typed(FILE *f) {
    (void)f;
}

static void generate_python_pyi(FILE *f, const Model *m) {
    fprintf(f, "from typing import Any, ClassVar, Dict, List, Mapping, Optional, Union\n\n");
    fprintf(f, "FieldInfo = Dict[str, Any]\nMsgpackMap = Dict[Any, Any]\nschemas: Dict[str, List[FieldInfo]]\n\n");
    fprintf(f, "def load_c_library(library_path: Optional[str] = ...) -> Any: ...\n");
    fprintf(f, "def types() -> List[str]: ...\n");
    fprintf(f, "def fields(type_name: str) -> List[FieldInfo]: ...\n\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        fprintf(f, "class %s:\n", c->c_name);
        fprintf(f, "    __schema__: ClassVar[List[FieldInfo]]\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "    %s: %s\n", mem->c_name, python_optional_type_for(mem->cs_type));
        }
        fprintf(f, "    def __init__(self");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, ", %s: %s = ...", mem->c_name, python_optional_type_for(mem->cs_type));
        }
        fprintf(f, ") -> None: ...\n");
        fprintf(f, "    def to_msgpack_map(self) -> MsgpackMap: ...\n");
        fprintf(f, "    def encode(self) -> bytes: ...\n");
        fprintf(f, "    def from_msgpack_map(self, value: Mapping[Any, Any]) -> '%s': ...\n", c->c_name);
        fprintf(f, "    def decode(self, data: bytes) -> '%s': ...\n\n", c->c_name);
    }
}


static void generate_python_readme(FILE *f) {
    fprintf(f, "# Python object wrapper\n\n");
    fprintf(f, "This directory is a pip-installable package named `%s`. It has no Python MessagePack dependency; encode/decode calls the generated C bridge through `ctypes`.\n\n", PYTHON_PACKAGE_NAME);
    fprintf(f, "Build or install `libmsgpack_pjsekai` from this repository first, then set `MSGPACK_PJSEKAI_LIB` or call `load_c_library(path)` before encoding or decoding.\n\n");
    fprintf(f, "Install from PyPI after a release is published:\n\n");
    fprintf(f, "```sh\npython3 -m pip install %s\n```\n\n", PYTHON_PACKAGE_NAME);
    fprintf(f, "```sh\ncd wrappers/python\npython3 -m pip install .\n```\n\n");
    fprintf(f, "Publish through the repository `Publish Packages` GitHub Actions workflow, or manually with `python3 -m build` and `python3 -m twine upload dist/*` after PyPI trusted publishing or an API token is configured.\n\n");
    fprintf(f, "`msgpack_pjsekai/__init__.pyi` and `py.typed` contain one generated class per MessagePack struct for editor field hints.\n\n");
    fprintf(f, "```python\nfrom msgpack_pjsekai import load_c_library, Sekai_AssetBundleElement\n\nload_c_library('/path/to/libmsgpack_pjsekai.so')\n\nvalue = Sekai_AssetBundleElement(bundleName='example', crc=1234)\ndata = value.encode()  # bytes\nout = Sekai_AssetBundleElement().decode(data)\nprint(out.bundleName)\n```\n");
}


static void generate_go_wrapper(FILE *f, const Model *m) {
    fprintf(f, "package msgpackpjsekai\n\n");
    fprintf(f, "/*\n#cgo CFLAGS: -I.\n#cgo LDFLAGS: -lmsgpack_pjsekai\n#include <stdlib.h>\n#include <stdint.h>\n#include \"msgpack-pjsekai-bridge.h\"\n*/\nimport \"C\"\n\n");
    fprintf(f, "import (\n\t\"fmt\"\n\t\"reflect\"\n\t\"unsafe\"\n)\n\n");
    fprintf(f, "type FieldInfo struct {\n\tName string\n\tKeyKind string\n\tKey any\n}\n\n");
    fprintf(f, "var Schemas = map[string][]FieldInfo{\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        fprintf(f, "\t");
        fprint_json_string(f, c->c_name);
        fprintf(f, ": {\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "\t\t{Name: ");
            fprint_json_string(f, mem->c_name);
            fprintf(f, ", KeyKind: ");
            fprint_json_string(f, mem->key_kind == KEY_INT ? "int" : "string");
            fprintf(f, ", Key: ");
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "int64(%ld)", mem->key_int);
            fprintf(f, "},\n");
        }
        fprintf(f, "\t},\n");
    }
    fprintf(f, "}\n\n");
    fprintf(f, "func Types() []string {\n\tnames := make([]string, 0, len(Schemas))\n\tfor name := range Schemas {\n\t\tnames = append(names, name)\n\t}\n\treturn names\n}\n\n");
    fprintf(f, "func Fields(typeName string) []FieldInfo {\n\treturn append([]FieldInfo(nil), Schemas[typeName]...)\n}\n\n");
    fprintf(f, "type msgpackMapper interface { ToMsgpackMap() map[any]any }\n\n");
    fprintf(f, "func cBytes(data []byte) (*C.uint8_t, C.size_t) {\n\tif len(data) == 0 { return nil, 0 }\n\treturn (*C.uint8_t)(C.CBytes(data)), C.size_t(len(data))\n}\n\n");
    fprintf(f, "func valueFromGo(value any) *C.mpj_value {\n\tif value == nil { return C.mpj_value_new_nil() }\n\tswitch v := value.(type) {\n\tcase bool:\n\t\tif v { return C.mpj_value_new_bool(1) }\n\t\treturn C.mpj_value_new_bool(0)\n\tcase int:\n\t\treturn C.mpj_value_new_int(C.int64_t(v))\n\tcase int8:\n\t\treturn C.mpj_value_new_int(C.int64_t(v))\n\tcase int16:\n\t\treturn C.mpj_value_new_int(C.int64_t(v))\n\tcase int32:\n\t\treturn C.mpj_value_new_int(C.int64_t(v))\n\tcase int64:\n\t\treturn C.mpj_value_new_int(C.int64_t(v))\n\tcase uint:\n\t\treturn C.mpj_value_new_uint(C.uint64_t(v))\n\tcase uint8:\n\t\treturn C.mpj_value_new_uint(C.uint64_t(v))\n\tcase uint16:\n\t\treturn C.mpj_value_new_uint(C.uint64_t(v))\n\tcase uint32:\n\t\treturn C.mpj_value_new_uint(C.uint64_t(v))\n\tcase uint64:\n\t\treturn C.mpj_value_new_uint(C.uint64_t(v))\n\tcase float32:\n\t\treturn C.mpj_value_new_float(C.double(v))\n\tcase float64:\n\t\treturn C.mpj_value_new_float(C.double(v))\n\tcase string:\n\t\tp, n := cBytes([]byte(v)); if p != nil { defer C.free(unsafe.Pointer(p)) }\n\t\treturn C.mpj_value_new_string(p, n)\n\tcase []byte:\n\t\tp, n := cBytes(v); if p != nil { defer C.free(unsafe.Pointer(p)) }\n\t\treturn C.mpj_value_new_binary(p, n)\n\tcase msgpackMapper:\n\t\treturn valueFromGo(v.ToMsgpackMap())\n\t}\n\trv := reflect.ValueOf(value)\n\tswitch rv.Kind() {\n\tcase reflect.Slice, reflect.Array:\n\t\tout := C.mpj_value_new_array(C.size_t(rv.Len()))\n\t\tfor i := 0; i < rv.Len(); i++ { C.mpj_value_array_set(out, C.size_t(i), valueFromGo(rv.Index(i).Interface())) }\n\t\treturn out\n\tcase reflect.Map:\n\t\tout := C.mpj_value_new_map(C.size_t(rv.Len()))\n\t\titer := rv.MapRange(); i := 0\n\t\tfor iter.Next() { C.mpj_value_map_set(out, C.size_t(i), valueFromGo(iter.Key().Interface()), valueFromGo(iter.Value().Interface())); i++ }\n\t\treturn out\n\t}\n\treturn C.mpj_value_new_nil()\n}\n\n");
    fprintf(f, "func goFromValue(ptr *C.mpj_value) any {\n\tif ptr == nil { return nil }\n\tswitch C.mpj_value_kind(ptr) {\n\tcase 0:\n\t\treturn nil\n\tcase 1:\n\t\treturn C.mpj_value_bool(ptr) != 0\n\tcase 2:\n\t\treturn int64(C.mpj_value_int(ptr))\n\tcase 3:\n\t\treturn uint64(C.mpj_value_uint(ptr))\n\tcase 4:\n\t\treturn float64(C.mpj_value_float(ptr))\n\tcase 5:\n\t\tdata := C.mpj_value_data(ptr); size := C.mpj_value_size(ptr)\n\t\treturn string(C.GoBytes(unsafe.Pointer(data), C.int(size)))\n\tcase 6:\n\t\tdata := C.mpj_value_data(ptr); size := C.mpj_value_size(ptr)\n\t\treturn C.GoBytes(unsafe.Pointer(data), C.int(size))\n\tcase 7:\n\t\tsize := int(C.mpj_value_size(ptr)); out := make([]any, size)\n\t\tfor i := 0; i < size; i++ { out[i] = goFromValue(C.mpj_value_array_get(ptr, C.size_t(i))) }\n\t\treturn out\n\tcase 8:\n\t\tsize := int(C.mpj_value_size(ptr)); out := make(map[any]any, size)\n\t\tfor i := 0; i < size; i++ { out[goFromValue(C.mpj_value_map_key(ptr, C.size_t(i)))] = goFromValue(C.mpj_value_map_value(ptr, C.size_t(i))) }\n\t\treturn out\n\t}\n\treturn nil\n}\n\n");
    fprintf(f, "func packNative(value any) ([]byte, error) {\n\tptr := valueFromGo(value)\n\tif ptr == nil { return nil, fmt.Errorf(\"MessagePack encode failed\") }\n\tdefer C.mpj_value_free(ptr)\n\tbuf := C.mpj_value_pack_bytes(ptr)\n\tif buf == nil { return nil, fmt.Errorf(\"MessagePack encode failed\") }\n\tdefer C.mpj_buffer_delete(buf)\n\tdata := C.mpj_buffer_data(buf); size := C.mpj_buffer_size(buf)\n\treturn C.GoBytes(unsafe.Pointer(data), C.int(size)), nil\n}\n\n");
    fprintf(f, "func unpackNative(data []byte) (map[any]any, error) {\n\tp, n := cBytes(data)\n\tif p != nil { defer C.free(unsafe.Pointer(p)) }\n\tptr := C.mpj_value_unpack_bytes(p, n)\n\tif ptr == nil { return nil, fmt.Errorf(\"MessagePack decode failed\") }\n\tdefer C.mpj_value_free(ptr)\n\tvalue, ok := goFromValue(ptr).(map[any]any)\n\tif !ok { return nil, fmt.Errorf(\"expected MessagePack map\") }\n\treturn value, nil\n}\n\n");
    fprintf(f, "func lookupMsgpackKey(raw map[any]any, key any) (any, bool) {\n\tif value, ok := raw[key]; ok {\n\t\treturn value, true\n\t}\n\tif target, ok := numericToInt64(key); ok {\n\t\tfor actual, value := range raw {\n\t\t\tif n, ok := numericToInt64(actual); ok && n == target {\n\t\t\t\treturn value, true\n\t\t\t}\n\t\t}\n\t}\n\treturn nil, false\n}\n\n");
    fprintf(f, "func numericToInt64(value any) (int64, bool) {\n\tswitch v := value.(type) {\n\tcase int:\n\t\treturn int64(v), true\n\tcase int8:\n\t\treturn int64(v), true\n\tcase int16:\n\t\treturn int64(v), true\n\tcase int32:\n\t\treturn int64(v), true\n\tcase int64:\n\t\treturn v, true\n\tcase uint:\n\t\tif uint64(v) <= uint64(1<<63-1) { return int64(v), true }\n\tcase uint8:\n\t\treturn int64(v), true\n\tcase uint16:\n\t\treturn int64(v), true\n\tcase uint32:\n\t\treturn int64(v), true\n\tcase uint64:\n\t\tif v <= uint64(1<<63-1) { return int64(v), true }\n\t}\n\treturn 0, false\n}\n\n");
    fprintf(f, "func numericToUint64(value any) (uint64, bool) {\n\tswitch v := value.(type) {\n\tcase int:\n\t\tif v >= 0 { return uint64(v), true }\n\tcase int8:\n\t\tif v >= 0 { return uint64(v), true }\n\tcase int16:\n\t\tif v >= 0 { return uint64(v), true }\n\tcase int32:\n\t\tif v >= 0 { return uint64(v), true }\n\tcase int64:\n\t\tif v >= 0 { return uint64(v), true }\n\tcase uint:\n\t\treturn uint64(v), true\n\tcase uint8:\n\t\treturn uint64(v), true\n\tcase uint16:\n\t\treturn uint64(v), true\n\tcase uint32:\n\t\treturn uint64(v), true\n\tcase uint64:\n\t\treturn v, true\n\t}\n\treturn 0, false\n}\n\n");
    fprintf(f, "func toBool(value any) bool { v, _ := value.(bool); return v }\n");
    fprintf(f, "func toInt8(value any) int8 { v, _ := numericToInt64(value); return int8(v) }\n");
    fprintf(f, "func toUint8(value any) uint8 { v, _ := numericToUint64(value); return uint8(v) }\n");
    fprintf(f, "func toInt16(value any) int16 { v, _ := numericToInt64(value); return int16(v) }\n");
    fprintf(f, "func toUint16(value any) uint16 { v, _ := numericToUint64(value); return uint16(v) }\n");
    fprintf(f, "func toInt32(value any) int32 { v, _ := numericToInt64(value); return int32(v) }\n");
    fprintf(f, "func toUint32(value any) uint32 { v, _ := numericToUint64(value); return uint32(v) }\n");
    fprintf(f, "func toInt64(value any) int64 { v, _ := numericToInt64(value); return v }\n");
    fprintf(f, "func toUint64(value any) uint64 { v, _ := numericToUint64(value); return v }\n");
    fprintf(f, "func toFloat32(value any) float32 { return float32(toFloat64(value)) }\n");
    fprintf(f, "func toFloat64(value any) float64 {\n\tswitch v := value.(type) {\n\tcase float32:\n\t\treturn float64(v)\n\tcase float64:\n\t\treturn v\n\t}\n\tif n, ok := numericToInt64(value); ok { return float64(n) }\n\tif n, ok := numericToUint64(value); ok { return float64(n) }\n\treturn 0\n}\n");
    fprintf(f, "func toString(value any) string {\n\tswitch v := value.(type) {\n\tcase string:\n\t\treturn v\n\tcase []byte:\n\t\treturn string(v)\n\tdefault:\n\t\tif v == nil { return \"\" }\n\t\treturn fmt.Sprint(v)\n\t}\n}\n\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        char *name = export_ident(c->c_name);
        fprintf(f, "type %s struct {\n", name);
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            char *field = export_ident(mem->c_name);
            fprintf(f, "\t%s %s `json:\"%s,omitempty\"`\n", field, go_object_type_for(mem->cs_type), mem->c_name);
            free(field);
        }
        fprintf(f, "}\n\n");
        fprintf(f, "func (value %s) ToMsgpackMap() map[any]any {\n\tout := make(map[any]any, %zu)\n", name, c->member_count);
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            char *field = export_ident(mem->c_name);
            fprintf(f, "\tout[");
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "int64(%ld)", mem->key_int);
            fprintf(f, "] = value.%s\n", field);
            free(field);
        }
        fprintf(f, "\treturn out\n}\n\n");
        fprintf(f, "func (value %s) Encode() ([]byte, error) {\n\treturn packNative(value.ToMsgpackMap())\n}\n\n", name);
        fprintf(f, "func (value *%s) Decode(data []byte) error {\n\traw, err := unpackNative(data)\n\tif err != nil {\n\t\treturn err\n\t}\n\tvalue.decodeFromMsgpackMap(raw)\n\treturn nil\n}\n\n", name);
        fprintf(f, "func (value *%s) decodeFromMsgpackMap(raw map[any]any) {\n", name);
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            char *field = export_ident(mem->c_name);
            const char *helper = go_decode_helper_for(mem->cs_type);
            fprintf(f, "\tif item, ok := lookupMsgpackKey(raw, ");
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "int64(%ld)", mem->key_int);
            if (helper) fprintf(f, "); ok { value.%s = %s(item) }\n", field, helper);
            else fprintf(f, "); ok { value.%s = item }\n", field);
            free(field);
        }
        fprintf(f, "}\n\n");
        free(name);
    }
}


static void generate_go_mod(FILE *f) {
    fprintf(f, "module %s\n\n", GO_MODULE_PATH);
    fprintf(f, "go 1.21\n");
}


static void generate_go_readme(FILE *f) {
    fprintf(f, "# Go object wrapper\n\n");
    fprintf(f, "This directory is a Go module. It has no Go MessagePack dependency; encode/decode calls the generated C bridge through cgo.\n\n");
    fprintf(f, "Build or install `libmsgpack_pjsekai` from this repository first, then point cgo at its directory with `CGO_LDFLAGS=-L/path/to/lib` and set your platform runtime library path (`LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`, or `PATH`).\n\n");
    fprintf(f, "Install after a release tag is pushed:\n\n");
    fprintf(f, "```sh\ngo get %s@v%s\n```\n\n", GO_MODULE_PATH, PACKAGE_VERSION);
    fprintf(f, "```sh\ncd wrappers/go\ngo build ./...\n```\n\n");
    fprintf(f, "Publish by pushing the subdirectory module tag `msgpack/wrappers/go/v%s`; the repository `Publish Packages` workflow verifies this tag.\n\n", PACKAGE_VERSION);
    fprintf(f, "Use it from another module with a local replace while developing from this generated tree:\n\n");
    fprintf(f, "```sh\ngo mod edit -replace %s=/absolute/path/to/wrappers/go\ngo get %s\n```\n\n", GO_MODULE_PATH, GO_MODULE_PATH);
    fprintf(f, "```go\npackage main\n\nimport mpj \"%s\"\n\nfunc main() {\n    value := mpj.Sekai_AssetBundleElement{BundleName: \"example\", Crc: 1234}\n    data, _ := value.Encode()\n    var out mpj.Sekai_AssetBundleElement\n    _ = out.Decode(data)\n    _ = out.BundleName\n}\n```\n", GO_MODULE_PATH);
}


static void generate_java_wrapper(FILE *f, const Model *m) {
    fprintf(f, "package %s;\n\n", JAVA_PACKAGE_NAME);
    fprintf(f, "import java.io.IOException;\nimport java.util.ArrayList;\nimport java.util.Arrays;\nimport java.util.Collections;\nimport java.util.LinkedHashMap;\nimport java.util.List;\nimport java.util.Map;\n\n");
    fprintf(f, "public final class MsgpackPjsekai {\n    private MsgpackPjsekai() {}\n\n");
    fprintf(f, "    static {\n        System.loadLibrary(\"msgpack_pjsekai_jni\");\n    }\n\n");
    fprintf(f, "    private static native byte[] packNative(Map<Object, Object> values);\n    private static native Object unpackNative(byte[] data);\n\n");
    fprintf(f, "    public static final class FieldInfo {\n        public final String name;\n        public final String keyKind;\n        public final Object key;\n\n        public FieldInfo(String name, String keyKind, Object key) {\n            this.name = name;\n            this.keyKind = keyKind;\n            this.key = key;\n        }\n    }\n\n");
    fprintf(f, "    private static final Map<String, List<FieldInfo>> SCHEMAS = new LinkedHashMap<>();\n\n");
    fprintf(f, "    private static void register(String name, List<FieldInfo> fields) {\n        SCHEMAS.put(name, fields);\n    }\n\n");
    fprintf(f, "    static {\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        char *name = export_ident(m->classes[i].c_name);
        fprintf(f, "        register(\"%s\", %s.SCHEMA);\n", m->classes[i].c_name, name);
        free(name);
    }
    fprintf(f, "    }\n\n");
    fprintf(f, "    public static List<String> types() {\n        return Collections.unmodifiableList(new ArrayList<>(SCHEMAS.keySet()));\n    }\n\n");
    fprintf(f, "    public static List<FieldInfo> fields(String typeName) {\n        List<FieldInfo> fields = SCHEMAS.get(typeName);\n        if (fields == null) throw new IllegalArgumentException(\"unknown msgpack-pjsekai type: \" + typeName);\n        return fields;\n    }\n\n");
    fprintf(f, "    private static Object get(Map<?, ?> values, Object key) {\n        Object direct = values.get(key);\n        if (direct != null || values.containsKey(key)) return direct;\n        if (key instanceof Number) {\n            long target = ((Number)key).longValue();\n            for (Map.Entry<?, ?> entry : values.entrySet()) {\n                Object actual = entry.getKey();\n                if (actual instanceof Number && ((Number)actual).longValue() == target) return entry.getValue();\n            }\n        }\n        return null;\n    }\n\n");
    fprintf(f, "    private static Boolean asBoolean(Object value) {\n        return value instanceof Boolean ? (Boolean)value : null;\n    }\n\n");
    fprintf(f, "    private static Long asLong(Object value) {\n        return value instanceof Number ? Long.valueOf(((Number)value).longValue()) : null;\n    }\n\n");
    fprintf(f, "    private static Double asDouble(Object value) {\n        return value instanceof Number ? Double.valueOf(((Number)value).doubleValue()) : null;\n    }\n\n");
    fprintf(f, "    private static String asString(Object value) {\n        return value == null ? null : value.toString();\n    }\n\n");
    fprintf(f, "    private static Object asObject(Object value) {\n        return value;\n    }\n\n");
    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        char *name = export_ident(c->c_name);
        fprintf(f, "    public static final class %s {\n", name);
        fprintf(f, "        public static final List<FieldInfo> SCHEMA = Collections.unmodifiableList(Arrays.asList(\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "            new FieldInfo(");
            fprint_json_string(f, mem->c_name);
            fprintf(f, ", ");
            fprint_json_string(f, mem->key_kind == KEY_INT ? "int" : "string");
            fprintf(f, ", ");
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "Long.valueOf(%ldL)", mem->key_int);
            fprintf(f, ")%s\n", j + 1 == c->member_count ? "" : ",");
        }
        fprintf(f, "        ));\n\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "        public %s %s;\n", java_object_type_for(mem->cs_type), mem->c_name);
        }
        fprintf(f, "\n        public Map<Object, Object> toMsgpackMap() {\n            Map<Object, Object> out = new LinkedHashMap<>();\n");
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "            if (this.%s != null) out.put(", mem->c_name);
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "Long.valueOf(%ldL)", mem->key_int);
            fprintf(f, ", this.%s);\n", mem->c_name);
        }
        fprintf(f, "            return out;\n        }\n\n");
        fprintf(f, "        public byte[] encode() throws IOException {\n            return packNative(toMsgpackMap());\n        }\n\n");
        fprintf(f, "        public %s decode(byte[] data) throws IOException {\n            Object unpacked = unpackNative(data);\n            if (!(unpacked instanceof Map)) throw new IOException(\"expected MessagePack map\");\n            Map<?, ?> raw = (Map<?, ?>)unpacked;\n            Object value;\n", name);
        for (size_t j = 0; j < c->member_count; ++j) {
            const Member *mem = &c->members[j];
            fprintf(f, "            value = get(raw, ");
            if (mem->key_kind == KEY_STRING) fprint_json_string(f, mem->key_string);
            else fprintf(f, "Long.valueOf(%ldL)", mem->key_int);
            fprintf(f, "); if (value != null) this.%s = %s(value);\n", mem->c_name, java_decode_helper_for(mem->cs_type));
        }
        fprintf(f, "            return this;\n        }\n    }\n\n");
        free(name);
    }
    fprintf(f, "}\n");
}


static void generate_java_jni_source(FILE *f) {
    fprintf(f, "#include <jni.h>\n#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n\n");
    fprintf(f, "#include \"msgpack-pjsekai-bridge.h\"\n\n");
    fprintf(f, "static void throw_java_exception(JNIEnv *env, const char *class_name, const char *message) {\n    jclass cls = (*env)->FindClass(env, class_name);\n    if (cls) (*env)->ThrowNew(env, cls, message);\n}\n\n");
    fprintf(f, "static int is_instance(JNIEnv *env, jobject object, const char *class_name) {\n    jclass cls = (*env)->FindClass(env, class_name);\n    return cls && (*env)->IsInstanceOf(env, object, cls);\n}\n\n");
    fprintf(f, "static mpj_value *value_from_java(JNIEnv *env, jobject object);\n\n");
    fprintf(f, "static mpj_value *value_from_byte_array(JNIEnv *env, jbyteArray array) {\n    jsize size = (*env)->GetArrayLength(env, array);\n    jbyte *data = (*env)->GetByteArrayElements(env, array, NULL);\n    if (!data) return NULL;\n    mpj_value *out = mpj_value_new_binary((const uint8_t *)data, (size_t)size);\n    (*env)->ReleaseByteArrayElements(env, array, data, JNI_ABORT);\n    return out;\n}\n\n");
    fprintf(f, "static mpj_value *value_from_string(JNIEnv *env, jstring string) {\n    const char *data = (*env)->GetStringUTFChars(env, string, NULL);\n    if (!data) return NULL;\n    mpj_value *out = mpj_value_new_string((const uint8_t *)data, strlen(data));\n    (*env)->ReleaseStringUTFChars(env, string, data);\n    return out;\n}\n\n");
    fprintf(f, "static mpj_value *value_from_map(JNIEnv *env, jobject map) {\n    jclass map_cls = (*env)->FindClass(env, \"java/util/Map\");\n    jmethodID size_mid = (*env)->GetMethodID(env, map_cls, \"size\", \"()I\");\n    jmethodID entry_set_mid = (*env)->GetMethodID(env, map_cls, \"entrySet\", \"()Ljava/util/Set;\");\n    jint size = (*env)->CallIntMethod(env, map, size_mid);\n    mpj_value *out = mpj_value_new_map((size_t)size);\n    jobject set = (*env)->CallObjectMethod(env, map, entry_set_mid);\n    jclass iterable_cls = (*env)->FindClass(env, \"java/lang/Iterable\");\n    jmethodID iterator_mid = (*env)->GetMethodID(env, iterable_cls, \"iterator\", \"()Ljava/util/Iterator;\");\n    jobject iterator = (*env)->CallObjectMethod(env, set, iterator_mid);\n    jclass iterator_cls = (*env)->FindClass(env, \"java/util/Iterator\");\n    jmethodID has_next_mid = (*env)->GetMethodID(env, iterator_cls, \"hasNext\", \"()Z\");\n    jmethodID next_mid = (*env)->GetMethodID(env, iterator_cls, \"next\", \"()Ljava/lang/Object;\");\n    jclass entry_cls = (*env)->FindClass(env, \"java/util/Map$Entry\");\n    jmethodID get_key_mid = (*env)->GetMethodID(env, entry_cls, \"getKey\", \"()Ljava/lang/Object;\");\n    jmethodID get_value_mid = (*env)->GetMethodID(env, entry_cls, \"getValue\", \"()Ljava/lang/Object;\");\n    size_t index = 0;\n    while ((*env)->CallBooleanMethod(env, iterator, has_next_mid)) {\n        jobject entry = (*env)->CallObjectMethod(env, iterator, next_mid);\n        jobject key = (*env)->CallObjectMethod(env, entry, get_key_mid);\n        jobject value = (*env)->CallObjectMethod(env, entry, get_value_mid);\n        mpj_value_map_set(out, index++, value_from_java(env, key), value_from_java(env, value));\n    }\n    return out;\n}\n\n");
    fprintf(f, "static mpj_value *value_from_collection(JNIEnv *env, jobject collection) {\n    jclass collection_cls = (*env)->FindClass(env, \"java/util/Collection\");\n    jmethodID size_mid = (*env)->GetMethodID(env, collection_cls, \"size\", \"()I\");\n    jint size = (*env)->CallIntMethod(env, collection, size_mid);\n    mpj_value *out = mpj_value_new_array((size_t)size);\n    jclass iterable_cls = (*env)->FindClass(env, \"java/lang/Iterable\");\n    jmethodID iterator_mid = (*env)->GetMethodID(env, iterable_cls, \"iterator\", \"()Ljava/util/Iterator;\");\n    jobject iterator = (*env)->CallObjectMethod(env, collection, iterator_mid);\n    jclass iterator_cls = (*env)->FindClass(env, \"java/util/Iterator\");\n    jmethodID has_next_mid = (*env)->GetMethodID(env, iterator_cls, \"hasNext\", \"()Z\");\n    jmethodID next_mid = (*env)->GetMethodID(env, iterator_cls, \"next\", \"()Ljava/lang/Object;\");\n    size_t index = 0;\n    while ((*env)->CallBooleanMethod(env, iterator, has_next_mid)) {\n        jobject item = (*env)->CallObjectMethod(env, iterator, next_mid);\n        mpj_value_array_set(out, index++, value_from_java(env, item));\n    }\n    return out;\n}\n\n");
    fprintf(f, "static mpj_value *value_from_java(JNIEnv *env, jobject object) {\n    if (!object) return mpj_value_new_nil();\n    if (is_instance(env, object, \"java/lang/Boolean\")) {\n        jclass cls = (*env)->FindClass(env, \"java/lang/Boolean\");\n        jmethodID mid = (*env)->GetMethodID(env, cls, \"booleanValue\", \"()Z\");\n        return mpj_value_new_bool((*env)->CallBooleanMethod(env, object, mid));\n    }\n    if (is_instance(env, object, \"java/lang/Float\") || is_instance(env, object, \"java/lang/Double\")) {\n        jclass cls = (*env)->FindClass(env, \"java/lang/Number\");\n        jmethodID mid = (*env)->GetMethodID(env, cls, \"doubleValue\", \"()D\");\n        return mpj_value_new_float((*env)->CallDoubleMethod(env, object, mid));\n    }\n    if (is_instance(env, object, \"java/lang/Number\")) {\n        jclass cls = (*env)->FindClass(env, \"java/lang/Number\");\n        jmethodID mid = (*env)->GetMethodID(env, cls, \"longValue\", \"()J\");\n        jlong value = (*env)->CallLongMethod(env, object, mid);\n        return value >= 0 ? mpj_value_new_uint((uint64_t)value) : mpj_value_new_int((int64_t)value);\n    }\n    if (is_instance(env, object, \"java/lang/String\")) return value_from_string(env, (jstring)object);\n    if ((*env)->IsInstanceOf(env, object, (*env)->FindClass(env, \"[B\"))) return value_from_byte_array(env, (jbyteArray)object);\n    if (is_instance(env, object, \"java/util/Map\")) return value_from_map(env, object);\n    if (is_instance(env, object, \"java/util/Collection\")) return value_from_collection(env, object);\n    return mpj_value_new_nil();\n}\n\n");
    fprintf(f, "static jobject java_from_value(JNIEnv *env, const mpj_value *value) {\n    int kind = mpj_value_kind(value);\n    if (kind == MPJ_VALUE_NIL || kind < 0) return NULL;\n    if (kind == MPJ_VALUE_BOOL) {\n        jclass cls = (*env)->FindClass(env, \"java/lang/Boolean\");\n        jmethodID mid = (*env)->GetStaticMethodID(env, cls, \"valueOf\", \"(Z)Ljava/lang/Boolean;\");\n        return (*env)->CallStaticObjectMethod(env, cls, mid, (jboolean)mpj_value_bool(value));\n    }\n    if (kind == MPJ_VALUE_INT || kind == MPJ_VALUE_UINT) {\n        jclass cls = (*env)->FindClass(env, \"java/lang/Long\");\n        jmethodID mid = (*env)->GetStaticMethodID(env, cls, \"valueOf\", \"(J)Ljava/lang/Long;\");\n        jlong n = kind == MPJ_VALUE_INT ? (jlong)mpj_value_int(value) : (jlong)mpj_value_uint(value);\n        return (*env)->CallStaticObjectMethod(env, cls, mid, n);\n    }\n    if (kind == MPJ_VALUE_FLOAT) {\n        jclass cls = (*env)->FindClass(env, \"java/lang/Double\");\n        jmethodID mid = (*env)->GetStaticMethodID(env, cls, \"valueOf\", \"(D)Ljava/lang/Double;\");\n        return (*env)->CallStaticObjectMethod(env, cls, mid, (jdouble)mpj_value_float(value));\n    }\n    if (kind == MPJ_VALUE_STRING) {\n        size_t size = mpj_value_size(value);\n        const uint8_t *data = mpj_value_data(value);\n        char *tmp = (char *)malloc(size + 1);\n        if (!tmp) return NULL;\n        memcpy(tmp, data, size);\n        tmp[size] = '\\0';\n        jstring out = (*env)->NewStringUTF(env, tmp);\n        free(tmp);\n        return out;\n    }\n    if (kind == MPJ_VALUE_BINARY) {\n        size_t size = mpj_value_size(value);\n        const uint8_t *data = mpj_value_data(value);\n        jbyteArray out = (*env)->NewByteArray(env, (jsize)size);\n        if (out && size > 0) (*env)->SetByteArrayRegion(env, out, 0, (jsize)size, (const jbyte *)data);\n        return out;\n    }\n    if (kind == MPJ_VALUE_ARRAY) {\n        jclass cls = (*env)->FindClass(env, \"java/util/ArrayList\");\n        jmethodID init = (*env)->GetMethodID(env, cls, \"<init>\", \"(I)V\");\n        jmethodID add = (*env)->GetMethodID(env, cls, \"add\", \"(Ljava/lang/Object;)Z\");\n        size_t size = mpj_value_size(value);\n        jobject out = (*env)->NewObject(env, cls, init, (jint)size);\n        for (size_t i = 0; i < size; ++i) (*env)->CallBooleanMethod(env, out, add, java_from_value(env, mpj_value_array_get(value, i)));\n        return out;\n    }\n    if (kind == MPJ_VALUE_MAP) {\n        jclass cls = (*env)->FindClass(env, \"java/util/LinkedHashMap\");\n        jmethodID init = (*env)->GetMethodID(env, cls, \"<init>\", \"(I)V\");\n        jmethodID put = (*env)->GetMethodID(env, cls, \"put\", \"(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\");\n        size_t size = mpj_value_size(value);\n        jobject out = (*env)->NewObject(env, cls, init, (jint)size);\n        for (size_t i = 0; i < size; ++i) (*env)->CallObjectMethod(env, out, put, java_from_value(env, mpj_value_map_key(value, i)), java_from_value(env, mpj_value_map_value(value, i)));\n        return out;\n    }\n    return NULL;\n}\n\n");
    fprintf(f, "JNIEXPORT jbyteArray JNICALL Java_io_github_sekaiworld_msgpackpjsekai_MsgpackPjsekai_packNative(JNIEnv *env, jclass cls, jobject map) {\n    (void)cls;\n    mpj_value *value = value_from_java(env, map);\n    if (!value) { throw_java_exception(env, \"java/lang/IllegalStateException\", \"MessagePack encode failed\"); return NULL; }\n    mpj_buffer *buffer = mpj_value_pack_bytes(value);\n    mpj_value_free(value);\n    if (!buffer) { throw_java_exception(env, \"java/lang/IllegalStateException\", \"MessagePack encode failed\"); return NULL; }\n    size_t size = mpj_buffer_size(buffer);\n    const uint8_t *data = mpj_buffer_data(buffer);\n    jbyteArray out = (*env)->NewByteArray(env, (jsize)size);\n    if (out && size > 0) (*env)->SetByteArrayRegion(env, out, 0, (jsize)size, (const jbyte *)data);\n    mpj_buffer_delete(buffer);\n    return out;\n}\n\n");
    fprintf(f, "JNIEXPORT jobject JNICALL Java_io_github_sekaiworld_msgpackpjsekai_MsgpackPjsekai_unpackNative(JNIEnv *env, jclass cls, jbyteArray input) {\n    (void)cls;\n    jsize size = (*env)->GetArrayLength(env, input);\n    jbyte *data = (*env)->GetByteArrayElements(env, input, NULL);\n    if (!data) return NULL;\n    mpj_value *value = mpj_value_unpack_bytes((const uint8_t *)data, (size_t)size);\n    (*env)->ReleaseByteArrayElements(env, input, data, JNI_ABORT);\n    if (!value) { throw_java_exception(env, \"java/lang/IllegalStateException\", \"MessagePack decode failed\"); return NULL; }\n    jobject out = java_from_value(env, value);\n    mpj_value_free(value);\n    return out;\n}\n");
}





static void generate_java_pom(FILE *f) {
    fprintf(f, "<project xmlns=\"http://maven.apache.org/POM/4.0.0\"\n");
    fprintf(f, "         xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
    fprintf(f, "         xsi:schemaLocation=\"http://maven.apache.org/POM/4.0.0 https://maven.apache.org/xsd/maven-4.0.0.xsd\">\n");
    fprintf(f, "  <modelVersion>4.0.0</modelVersion>\n");
    fprintf(f, "  <groupId>%s</groupId>\n", MAVEN_GROUP_ID);
    fprintf(f, "  <artifactId>%s</artifactId>\n", MAVEN_ARTIFACT_ID);
    fprintf(f, "  <version>%s</version>\n", PACKAGE_VERSION);
    fprintf(f, "  <packaging>jar</packaging>\n");
    fprintf(f, "  <name>msgpack-pjsekai Java object wrapper</name>\n");
    fprintf(f, "  <description>Static object-based MessagePack models generated from dump.cs.</description>\n");
    fprintf(f, "  <url>%s</url>\n", PROJECT_URL);
    fprintf(f, "  <licenses>\n");
    fprintf(f, "    <license>\n");
    fprintf(f, "      <name>MIT License</name>\n");
    fprintf(f, "      <url>https://opensource.org/licenses/MIT</url>\n");
    fprintf(f, "    </license>\n");
    fprintf(f, "  </licenses>\n");
    fprintf(f, "  <developers>\n");
    fprintf(f, "    <developer>\n");
    fprintf(f, "      <id>Sekai-World</id>\n");
    fprintf(f, "      <name>Sekai-World</name>\n");
    fprintf(f, "    </developer>\n");
    fprintf(f, "  </developers>\n");
    fprintf(f, "  <scm>\n");
    fprintf(f, "    <connection>scm:git:%s.git</connection>\n", PROJECT_URL);
    fprintf(f, "    <developerConnection>scm:git:%s.git</developerConnection>\n", PROJECT_URL);
    fprintf(f, "    <url>%s</url>\n", PROJECT_URL);
    fprintf(f, "  </scm>\n");
    fprintf(f, "  <properties>\n");
    fprintf(f, "    <maven.compiler.release>8</maven.compiler.release>\n");
    fprintf(f, "    <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>\n");
    fprintf(f, "  </properties>\n");
    fprintf(f, "  <dependencies />\n");
    fprintf(f, "  <build>\n");
    fprintf(f, "    <plugins>\n");
    fprintf(f, "      <plugin>\n");
    fprintf(f, "        <groupId>org.apache.maven.plugins</groupId>\n");
    fprintf(f, "        <artifactId>maven-compiler-plugin</artifactId>\n");
    fprintf(f, "        <version>3.13.0</version>\n");
    fprintf(f, "      </plugin>\n");
    fprintf(f, "    </plugins>\n");
    fprintf(f, "  </build>\n");
    fprintf(f, "  <profiles>\n");
    fprintf(f, "    <profile>\n");
    fprintf(f, "      <id>release</id>\n");
    fprintf(f, "      <build>\n");
    fprintf(f, "        <plugins>\n");
    fprintf(f, "          <plugin>\n");
    fprintf(f, "            <groupId>org.apache.maven.plugins</groupId>\n");
    fprintf(f, "            <artifactId>maven-source-plugin</artifactId>\n");
    fprintf(f, "            <version>3.3.1</version>\n");
    fprintf(f, "            <executions>\n");
    fprintf(f, "              <execution>\n");
    fprintf(f, "                <id>attach-sources</id>\n");
    fprintf(f, "                <goals><goal>jar-no-fork</goal></goals>\n");
    fprintf(f, "              </execution>\n");
    fprintf(f, "            </executions>\n");
    fprintf(f, "          </plugin>\n");
    fprintf(f, "          <plugin>\n");
    fprintf(f, "            <groupId>org.apache.maven.plugins</groupId>\n");
    fprintf(f, "            <artifactId>maven-javadoc-plugin</artifactId>\n");
    fprintf(f, "            <version>3.10.1</version>\n");
    fprintf(f, "            <configuration><doclint>none</doclint></configuration>\n");
    fprintf(f, "            <executions>\n");
    fprintf(f, "              <execution>\n");
    fprintf(f, "                <id>attach-javadocs</id>\n");
    fprintf(f, "                <goals><goal>jar</goal></goals>\n");
    fprintf(f, "              </execution>\n");
    fprintf(f, "            </executions>\n");
    fprintf(f, "          </plugin>\n");
    fprintf(f, "          <plugin>\n");
    fprintf(f, "            <groupId>org.apache.maven.plugins</groupId>\n");
    fprintf(f, "            <artifactId>maven-gpg-plugin</artifactId>\n");
    fprintf(f, "            <version>3.2.7</version>\n");
    fprintf(f, "            <configuration>\n");
    fprintf(f, "              <gpgArguments>\n");
    fprintf(f, "                <arg>--pinentry-mode</arg>\n");
    fprintf(f, "                <arg>loopback</arg>\n");
    fprintf(f, "              </gpgArguments>\n");
    fprintf(f, "            </configuration>\n");
    fprintf(f, "            <executions>\n");
    fprintf(f, "              <execution>\n");
    fprintf(f, "                <id>sign-artifacts</id>\n");
    fprintf(f, "                <phase>verify</phase>\n");
    fprintf(f, "                <goals><goal>sign</goal></goals>\n");
    fprintf(f, "              </execution>\n");
    fprintf(f, "            </executions>\n");
    fprintf(f, "          </plugin>\n");
    fprintf(f, "          <plugin>\n");
    fprintf(f, "            <groupId>org.sonatype.central</groupId>\n");
    fprintf(f, "            <artifactId>central-publishing-maven-plugin</artifactId>\n");
    fprintf(f, "            <version>0.9.0</version>\n");
    fprintf(f, "            <extensions>true</extensions>\n");
    fprintf(f, "            <configuration>\n");
    fprintf(f, "              <publishingServerId>central</publishingServerId>\n");
    fprintf(f, "              <autoPublish>true</autoPublish>\n");
    fprintf(f, "            </configuration>\n");
    fprintf(f, "          </plugin>\n");
    fprintf(f, "        </plugins>\n");
    fprintf(f, "      </build>\n");
    fprintf(f, "    </profile>\n");
    fprintf(f, "  </profiles>\n");
    fprintf(f, "</project>\n");
}


static void generate_java_readme(FILE *f) {
    fprintf(f, "# Java object wrapper\n\n");
    fprintf(f, "This directory is a Maven package `%s:%s`. It has no Java MessagePack dependency; encode/decode calls the generated C bridge through JNI.\n\n", MAVEN_GROUP_ID, MAVEN_ARTIFACT_ID);
    fprintf(f, "Build or install `libmsgpack_pjsekai` plus the generated JNI library `libmsgpack_pjsekai_jni`, then put both on `java.library.path` or the platform runtime library path before calling `encode`/`decode`.\n\n");
    fprintf(f, "Install from Maven Central after a release is published:\n\n");
    fprintf(f, "```xml\n<dependency>\n  <groupId>%s</groupId>\n  <artifactId>%s</artifactId>\n  <version>%s</version>\n</dependency>\n```\n\n", MAVEN_GROUP_ID, MAVEN_ARTIFACT_ID, PACKAGE_VERSION);
    fprintf(f, "```sh\ncd wrappers/java\nmvn package\n```\n\n");
    fprintf(f, "Publish through the repository `Publish Packages` GitHub Actions workflow, or manually with `mvn -Prelease -DskipTests deploy` after Maven Central credentials and GPG signing are configured.\n\n");
    fprintf(f, "The generated classes are nested under `%s.MsgpackPjsekai`.\n\n", JAVA_PACKAGE_NAME);
    fprintf(f, "```java\nimport io.github.sekaiworld.msgpackpjsekai.MsgpackPjsekai;\n\nMsgpackPjsekai.Sekai_AssetBundleElement value = new MsgpackPjsekai.Sekai_AssetBundleElement();\nvalue.bundleName = \"example\";\nvalue.crc = 1234L;\n\nbyte[] data = value.encode();\nMsgpackPjsekai.Sekai_AssetBundleElement out = new MsgpackPjsekai.Sekai_AssetBundleElement().decode(data);\nSystem.out.println(out.bundleName);\n```\n");
}



static int parse_dump(const char *path, Model *model) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
        return 1;
    }
    char *line = NULL;
    size_t cap = 0;
    char *current_ns = xstrdup("");
    bool pending_msgpack = false;
    bool in_class = false;
    Class current = {0};
    bool pending_key = false;
    KeyKind key_kind = KEY_STRING;
    char *key_string = NULL;
    long key_int = 0;

    while (getline(&line, &cap, fp) != -1) {
        char *s = trim(line);
        if (starts_with(s, "// Namespace:")) {
            free(current_ns);
            current_ns = xstrdup(trim(s + strlen("// Namespace:")));
            continue;
        }
        if (!in_class && strstr(s, "[MessagePackObject")) {
            pending_msgpack = true;
            continue;
        }
        if (!in_class && pending_msgpack) {
            char *name = extract_type_name(s);
            if (name) {
                memset(&current, 0, sizeof(current));
                current.name = name;
                current.ns = xstrdup(current_ns);
                current.c_name = class_c_name(current.ns, current.name);
                in_class = true;
                pending_msgpack = false;
            }
            continue;
        }
        if (!in_class) continue;
        if (strcmp(s, "}") == 0) {
            model_add_class(model, current);
            memset(&current, 0, sizeof(current));
            in_class = false;
            pending_key = false;
            free(key_string);
            key_string = NULL;
            continue;
        }
        KeyKind kk;
        char *ks = NULL;
        long ki = 0;
        if (parse_key_attr(s, &kk, &ks, &ki)) {
            if (pending_key) free(key_string);
            pending_key = true;
            key_kind = kk;
            key_string = ks;
            key_int = ki;
            continue;
        }
        if (pending_key) {
            Member mem;
            if (parse_member_line(s, &mem, key_kind, key_string, key_int)) {
                class_add_member(&current, mem);
                key_string = NULL;
                pending_key = false;
            }
        }
    }
    free(line);
    free(current_ns);
    fclose(fp);
    if (in_class) model_add_class(model, current);
    return 0;
}

static int write_generated(const char *out_dir, const Model *m, const char *source_name) {
    char gen_dir[4096];
    char wrappers_dir[4096];
    char js_dir[4096];
    char python_dir[4096];
    char python_pkg_dir[4096];
    char go_dir[4096];
    char java_dir[4096];
    char java_src_dir[4096];
    char java_native_dir[4096];
    snprintf(gen_dir, sizeof(gen_dir), "%s/generated", out_dir);
    snprintf(wrappers_dir, sizeof(wrappers_dir), "%s/wrappers", out_dir);
    snprintf(js_dir, sizeof(js_dir), "%s/js", wrappers_dir);
    snprintf(python_dir, sizeof(python_dir), "%s/python", wrappers_dir);
    snprintf(python_pkg_dir, sizeof(python_pkg_dir), "%s/msgpack_pjsekai", python_dir);
    snprintf(go_dir, sizeof(go_dir), "%s/go", wrappers_dir);
    snprintf(java_dir, sizeof(java_dir), "%s/java", wrappers_dir);
    snprintf(java_src_dir, sizeof(java_src_dir), "%s/src/main/java/%s", java_dir, JAVA_PACKAGE_PATH);
    snprintf(java_native_dir, sizeof(java_native_dir), "%s/src/main/native", java_dir);
    if (ensure_dir(out_dir) != 0) return 1;
    if (ensure_dir(gen_dir) != 0) return 1;
    if (ensure_dir(wrappers_dir) != 0) return 1;
    if (ensure_dir(js_dir) != 0) return 1;
    if (ensure_dir(python_dir) != 0) return 1;
    if (ensure_dir(python_pkg_dir) != 0) return 1;
    if (ensure_dir(go_dir) != 0) return 1;
    if (ensure_dir(java_dir) != 0) return 1;
    if (ensure_dir(java_src_dir) != 0) return 1;
    if (ensure_dir(java_native_dir) != 0) return 1;

    char path[4096];
    FILE *f = NULL;

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai.h", out_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_umbrella_header(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai-common.h", gen_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_common_header(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai-common.c", gen_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_common_source(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai-bridge.h", gen_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_bridge_header(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai-bridge.h", go_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_bridge_header(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai-bridge.h", java_native_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_bridge_header(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai-bridge.c", gen_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_bridge_source(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai.files", out_dir);
    if (open_plain(&f, path) != 0) return 1;
    fprintf(f, "generated/msgpack-pjsekai-common.c\n");
    fprintf(f, "generated/msgpack-pjsekai-bridge.c\n");

    for (size_t i = 0; i < m->class_count; ++i) {
        const Class *c = &m->classes[i];
        char hpath[4096];
        char cpath[4096];
        snprintf(hpath, sizeof(hpath), "%s/%s.h", gen_dir, c->c_name);
        snprintf(cpath, sizeof(cpath), "%s/%s.c", gen_dir, c->c_name);

        FILE *hf = NULL;
        if (open_generated(&hf, hpath, source_name) != 0) { fclose(f); return 1; }
        generate_class_header(hf, c);
        fclose(hf);

        FILE *cf = NULL;
        if (open_generated(&cf, cpath, source_name) != 0) { fclose(f); return 1; }
        generate_class_source(cf, c);
        fclose(cf);

        fprintf(f, "generated/%s.c\n", c->c_name);
    }
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai.js", js_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_js_wrapper(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai.d.ts", js_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_js_types(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack-pjsekai-wasm.d.ts", js_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_js_wasm_types(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/build-wasm.sh", js_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_js_wasm_build_script(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/package.json", js_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_js_package_json(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/README.md", js_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_js_readme(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/__init__.py", python_pkg_dir);
    if (open_generated_hash(&f, path, source_name) != 0) return 1;
    generate_python_wrapper(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/__init__.pyi", python_pkg_dir);
    if (open_generated_hash(&f, path, source_name) != 0) return 1;
    generate_python_pyi(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/py.typed", python_pkg_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_python_py_typed(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/pyproject.toml", python_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_python_pyproject(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/MANIFEST.in", python_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_python_manifest(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/README.md", python_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_python_readme(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack_pjsekai.go", go_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_go_wrapper(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/go.mod", go_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_go_mod(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/README.md", go_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_go_readme(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/MsgpackPjsekai.java", java_src_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_java_wrapper(f, m);
    fclose(f);

    snprintf(path, sizeof(path), "%s/msgpack_pjsekai_jni.c", java_native_dir);
    if (open_generated(&f, path, source_name) != 0) return 1;
    generate_java_jni_source(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/pom.xml", java_dir);
    if (open_xml(&f, path) != 0) return 1;
    generate_java_pom(f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/README.md", java_dir);
    if (open_plain(&f, path) != 0) return 1;
    generate_java_readme(f);
    fclose(f);

    printf("generated %zu MessagePackObject type(s) into %s/generated\n", m->class_count, out_dir);
    printf("umbrella header: %s/msgpack-pjsekai.h\n", out_dir);
    printf("source list: %s/msgpack-pjsekai.files\n", out_dir);
    printf("wrappers: %s/wrappers/{js,python,go,java}\n", out_dir);
    return 0;
}

int main(int argc, char **argv) {
    const char *dump_path = argc > 1 ? argv[1] : "dump.cs";
    const char *out_dir = argc > 2 ? argv[2] : "build/generated-msgpack";
    if (argc > 3 || strcmp(dump_path, "--help") == 0 || strcmp(dump_path, "-h") == 0) {
        fprintf(stderr, "usage: %s [dump.cs] [output-directory]\n", argv[0]);
        fprintf(stderr, "Generates split msgpack-c files under [output-directory]/generated for dump.cs [MessagePackObject] types.\n");
        return argc > 3 ? 1 : 0;
    }
    Model model = {0};
    int rc = parse_dump(dump_path, &model);
    if (rc != 0) return rc;
    return write_generated(out_dir, &model, dump_path);
}
