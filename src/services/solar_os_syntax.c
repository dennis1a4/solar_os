#include "solar_os_syntax.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>

static bool syntax_has_suffix(const char *path, const char *suffix)
{
    if (path == NULL || suffix == NULL) {
        return false;
    }

    const size_t path_len = strlen(path);
    const size_t suffix_len = strlen(suffix);
    return path_len >= suffix_len &&
        strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

solar_os_syntax_language_t solar_os_syntax_language_for_path(const char *path)
{
    if (syntax_has_suffix(path, ".py") || syntax_has_suffix(path, ".pyw")) {
        return SOLAR_OS_SYNTAX_PYTHON;
    }
    if (syntax_has_suffix(path, ".lua")) {
        return SOLAR_OS_SYNTAX_LUA;
    }
    return SOLAR_OS_SYNTAX_NONE;
}

void solar_os_syntax_state_init(solar_os_syntax_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->mode = SOLAR_OS_SYNTAX_MODE_NORMAL;
    state->lua_long_equals = 0;
}

static void syntax_mark(size_t visible_offset,
                        uint8_t *styles,
                        size_t visible_len,
                        size_t start,
                        size_t end,
                        solar_os_syntax_style_t style)
{
    if (styles == NULL || visible_len == 0 || start >= end) {
        return;
    }

    const size_t visible_end = visible_offset + visible_len;
    if (end <= visible_offset || start >= visible_end) {
        return;
    }

    size_t clipped_start = start > visible_offset ? start : visible_offset;
    size_t clipped_end = end < visible_end ? end : visible_end;
    for (size_t i = clipped_start; i < clipped_end; i++) {
        styles[i - visible_offset] = (uint8_t)style;
    }
}

static bool syntax_ident_start(char ch)
{
    const unsigned char value = (unsigned char)ch;

    return isalpha(value) || ch == '_';
}

static bool syntax_ident(char ch)
{
    const unsigned char value = (unsigned char)ch;

    return isalnum(value) || ch == '_';
}

static bool syntax_keyword_match(const char *word,
                                 size_t len,
                                 const char *const *keywords,
                                 size_t keyword_count)
{
    for (size_t i = 0; i < keyword_count; i++) {
        if (strlen(keywords[i]) == len && strncmp(word, keywords[i], len) == 0) {
            return true;
        }
    }
    return false;
}

static bool python_keyword(const char *word, size_t len)
{
    static const char *const keywords[] = {
        "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else", "except",
        "finally", "for", "from", "global", "if", "import", "in", "is",
        "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
        "while", "with", "yield",
    };

    return syntax_keyword_match(word, len, keywords, sizeof(keywords) / sizeof(keywords[0]));
}

static bool python_builtin(const char *word,size_t len)
{
    static const char *const names[]={"abs","all","any","bin","bool","bytearray","bytes",
        "callable","chr","classmethod","dict","dir","divmod","enumerate","eval","exec",
        "filter","float","getattr","hasattr","hash","hex","id","input","int","isinstance",
        "issubclass","iter","len","list","map","max","memoryview","min","next","object",
        "oct","open","ord","pow","print","property","range","repr","reversed","round",
        "set","setattr","slice","sorted","staticmethod","str","sum","super","tuple","type","zip"};
    return syntax_keyword_match(word,len,names,sizeof(names)/sizeof(*names));
}

static bool lua_keyword(const char *word, size_t len)
{
    static const char *const keywords[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while",
    };

    return syntax_keyword_match(word, len, keywords, sizeof(keywords) / sizeof(keywords[0]));
}

static size_t syntax_parse_number(const char *line, size_t line_len, size_t pos)
{
    size_t i = pos;

    if (i + 1 < line_len && line[i] == '0' &&
        (line[i + 1] == 'x' || line[i + 1] == 'X' ||
         line[i + 1] == 'b' || line[i + 1] == 'B' ||
         line[i + 1] == 'o' || line[i + 1] == 'O')) {
        i += 2;
    }

    while (i < line_len) {
        const unsigned char ch = (unsigned char)line[i];
        if (isalnum(ch) || ch == '_' || ch == '.') {
            i++;
            continue;
        }
        if ((ch == '+' || ch == '-') && i > pos) {
            const char prev = line[i - 1];
            if (prev == 'e' || prev == 'E' || prev == 'p' || prev == 'P') {
                i++;
                continue;
            }
        }
        break;
    }

    return i > pos ? i : pos + 1;
}

static size_t python_number(const char *line,size_t len,size_t i)
{
    if(i+1<len && line[i]=='0' && strchr("xXbBoO",line[i+1])) {
        char base=line[i+1];i+=2;
        while(i<len && (line[i]=='_' ||
            ((base=='x'||base=='X') ? isxdigit((unsigned char)line[i]) :
             (line[i]>='0' && line[i]<=((base=='b'||base=='B')?'1':'7')))))++i;
        return i;
    }
    while(i<len && (isdigit((unsigned char)line[i]) || line[i]=='_'))++i;
    if(i<len && line[i]=='.') {++i;while(i<len && (isdigit((unsigned char)line[i]) || line[i]=='_'))++i;}
    if(i<len && (line[i]=='e' || line[i]=='E')) {
        size_t end=i+1;if(end<len && (line[end]=='+' || line[end]=='-'))++end;
        if(end<len && isdigit((unsigned char)line[end])) {
            i=end+1;while(i<len && (isdigit((unsigned char)line[i]) || line[i]=='_'))++i;
        }
    }
    if(i<len && (line[i]=='j' || line[i]=='J'))++i;
    return i;
}

static bool syntax_match_repeated(const char *line,
                                  size_t line_len,
                                  size_t pos,
                                  char ch,
                                  size_t count)
{
    if (pos + count > line_len) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (line[pos + i] != ch) {
            return false;
        }
    }
    return true;
}

static size_t python_find_triple_end(const char *line, size_t line_len, size_t pos, char quote)
{
    while (pos + 2 < line_len) {
        if (line[pos] == '\\') { pos += 2; continue; }
        if (syntax_match_repeated(line, line_len, pos, quote, 3)) {
            return pos;
        }
        pos++;
    }
    return line_len;
}

static size_t python_parse_quote_string(const char *line, size_t line_len, size_t pos, char quote)
{
    size_t i = pos + 1;
    bool escaped = false;

    while (i < line_len) {
        const char ch = line[i++];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == quote) {
            break;
        }
    }

    return i;
}

static void python_highlight_line(solar_os_syntax_state_t *state,
                                  const char *line,
                                  size_t line_len,
                                  size_t visible_offset,
                                  uint8_t *styles,
                                  size_t visible_len)
{
    size_t i = 0;
    bool definition = false;

    if (state->mode == SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_SINGLE ||
        state->mode == SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_DOUBLE) {
        const char quote = state->mode == SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_SINGLE ? '\'' : '"';
        const size_t end = python_find_triple_end(line, line_len, 0, quote);
        if (end < line_len) {
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        0,
                        end + 3,
                        SOLAR_OS_SYNTAX_STYLE_STRING);
            state->mode = SOLAR_OS_SYNTAX_MODE_NORMAL;
            i = end + 3;
        } else {
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        0,
                        line_len,
                        SOLAR_OS_SYNTAX_STYLE_STRING);
            return;
        }
    }

    while (i < line_len) {
        const char ch = line[i];

        if (ch == '#') {
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        i,
                        line_len,
                        SOLAR_OS_SYNTAX_STYLE_COMMENT);
            return;
        }

        if (ch == '\'' || ch == '"') {
            const char quote = ch;
            if (syntax_match_repeated(line, line_len, i, quote, 3)) {
                const size_t end = python_find_triple_end(line, line_len, i + 3, quote);
                if (end < line_len) {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                end + 3,
                                SOLAR_OS_SYNTAX_STYLE_STRING);
                    i = end + 3;
                } else {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                line_len,
                                SOLAR_OS_SYNTAX_STYLE_STRING);
                    state->mode = quote == '\'' ?
                        SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_SINGLE :
                        SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_DOUBLE;
                    return;
                }
            } else {
                const size_t end = python_parse_quote_string(line, line_len, i, quote);
                syntax_mark(visible_offset,
                            styles,
                            visible_len,
                            i,
                            end,
                            SOLAR_OS_SYNTAX_STYLE_STRING);
                i = end;
            }
            continue;
        }

        if (isdigit((unsigned char)ch) || (ch=='.' && i+1<line_len && isdigit((unsigned char)line[i+1]))) {
            const size_t end = python_number(line, line_len, i);
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        i,
                        end,
                        SOLAR_OS_SYNTAX_STYLE_NUMBER);
            i = end;
            continue;
        }

        if (syntax_ident_start(ch)) {
            const size_t start = i;
            i++;
            while (i < line_len && syntax_ident(line[i])) {
                i++;
            }
            solar_os_syntax_style_t style=SOLAR_OS_SYNTAX_STYLE_NORMAL;
            size_t n=i-start;
            size_t previous=start;while(previous && isspace((unsigned char)line[previous-1]))--previous;
            bool string_prefix=i<line_len && (line[i]=='\'' || line[i]=='"') &&
                ((n==1 && strchr("rRbBuUfF",line[start])) ||
                 (n==2 && ((strchr("rR",line[start]) && strchr("bBfF",line[start+1])) ||
                           (strchr("bBfF",line[start]) && strchr("rR",line[start+1])))));
            if(string_prefix)style=SOLAR_OS_SYNTAX_STYLE_STRING;
            else if(definition) {style=SOLAR_OS_SYNTAX_STYLE_DEFINITION;definition=false;}
            else if((n==4 && !strncmp(line+start,"True",4)) || (n==5 && !strncmp(line+start,"False",5)) ||
                    (n==4 && !strncmp(line+start,"None",4))) style=SOLAR_OS_SYNTAX_STYLE_CONSTANT;
            else if(python_keyword(line+start,n)) {
                style=SOLAR_OS_SYNTAX_STYLE_KEYWORD;
                definition=(n==3 && !strncmp(line+start,"def",3)) || (n==5 && !strncmp(line+start,"class",5));
            } else if((previous==0 || line[previous-1]!='.') && python_builtin(line+start,n)) style=SOLAR_OS_SYNTAX_STYLE_BUILTIN;
            syntax_mark(visible_offset,styles,visible_len,start,i,style);
            continue;
        }

        i++;
    }
}

static bool lua_long_bracket_open(const char *line,
                                  size_t line_len,
                                  size_t pos,
                                  uint8_t *equals)
{
    if (pos >= line_len || line[pos] != '[') {
        return false;
    }

    size_t i = pos + 1;
    while (i < line_len && line[i] == '=') {
        if (i - pos > UINT8_MAX) {
            return false;
        }
        i++;
    }
    if (i >= line_len || line[i] != '[') {
        return false;
    }

    if (equals != NULL) {
        *equals = (uint8_t)(i - pos - 1);
    }
    return true;
}

static bool lua_long_bracket_close_at(const char *line,
                                      size_t line_len,
                                      size_t pos,
                                      uint8_t equals)
{
    if (pos >= line_len || line[pos] != ']') {
        return false;
    }

    size_t i = pos + 1;
    for (uint8_t eq = 0; eq < equals; eq++) {
        if (i >= line_len || line[i] != '=') {
            return false;
        }
        i++;
    }
    return i < line_len && line[i] == ']';
}

static size_t lua_long_bracket_end_len(uint8_t equals)
{
    return (size_t)equals + 2;
}

static size_t lua_find_long_bracket_end(const char *line,
                                        size_t line_len,
                                        size_t pos,
                                        uint8_t equals)
{
    while (pos < line_len) {
        if (lua_long_bracket_close_at(line, line_len, pos, equals)) {
            return pos;
        }
        pos++;
    }
    return line_len;
}

static size_t lua_parse_quote_string(const char *line, size_t line_len, size_t pos, char quote)
{
    return python_parse_quote_string(line, line_len, pos, quote);
}

static bool lua_continue_long(solar_os_syntax_state_t *state,
                              const char *line,
                              size_t line_len,
                              size_t visible_offset,
                              uint8_t *styles,
                              size_t visible_len,
                              size_t *pos)
{
    if (state->mode != SOLAR_OS_SYNTAX_MODE_LUA_LONG_STRING &&
        state->mode != SOLAR_OS_SYNTAX_MODE_LUA_LONG_COMMENT) {
        return false;
    }

    const size_t end = lua_find_long_bracket_end(line, line_len, 0, state->lua_long_equals);
    const solar_os_syntax_style_t style =
        state->mode == SOLAR_OS_SYNTAX_MODE_LUA_LONG_COMMENT ?
        SOLAR_OS_SYNTAX_STYLE_COMMENT :
        SOLAR_OS_SYNTAX_STYLE_STRING;
    if (end < line_len) {
        const size_t close_len = lua_long_bracket_end_len(state->lua_long_equals);
        syntax_mark(visible_offset, styles, visible_len, 0, end + close_len, style);
        state->mode = SOLAR_OS_SYNTAX_MODE_NORMAL;
        state->lua_long_equals = 0;
        *pos = end + close_len;
    } else {
        syntax_mark(visible_offset, styles, visible_len, 0, line_len, style);
        *pos = line_len;
    }
    return true;
}

static void lua_highlight_line(solar_os_syntax_state_t *state,
                               const char *line,
                               size_t line_len,
                               size_t visible_offset,
                               uint8_t *styles,
                               size_t visible_len)
{
    size_t i = 0;

    if (lua_continue_long(state, line, line_len, visible_offset, styles, visible_len, &i) &&
        state->mode != SOLAR_OS_SYNTAX_MODE_NORMAL) {
        return;
    }

    while (i < line_len) {
        const char ch = line[i];

        if (ch == '-' && i + 1 < line_len && line[i + 1] == '-') {
            uint8_t equals = 0;
            if (lua_long_bracket_open(line, line_len, i + 2, &equals)) {
                const size_t open_len = (size_t)equals + 4;
                const size_t end = lua_find_long_bracket_end(line, line_len, i + open_len, equals);
                if (end < line_len) {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                end + lua_long_bracket_end_len(equals),
                                SOLAR_OS_SYNTAX_STYLE_COMMENT);
                    i = end + lua_long_bracket_end_len(equals);
                } else {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                line_len,
                                SOLAR_OS_SYNTAX_STYLE_COMMENT);
                    state->mode = SOLAR_OS_SYNTAX_MODE_LUA_LONG_COMMENT;
                    state->lua_long_equals = equals;
                    return;
                }
            } else {
                syntax_mark(visible_offset,
                            styles,
                            visible_len,
                            i,
                            line_len,
                            SOLAR_OS_SYNTAX_STYLE_COMMENT);
                return;
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            const size_t end = lua_parse_quote_string(line, line_len, i, ch);
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        i,
                        end,
                        SOLAR_OS_SYNTAX_STYLE_STRING);
            i = end;
            continue;
        }

        if (ch == '[') {
            uint8_t equals = 0;
            if (lua_long_bracket_open(line, line_len, i, &equals)) {
                const size_t open_len = (size_t)equals + 2;
                const size_t end = lua_find_long_bracket_end(line, line_len, i + open_len, equals);
                if (end < line_len) {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                end + lua_long_bracket_end_len(equals),
                                SOLAR_OS_SYNTAX_STYLE_STRING);
                    i = end + lua_long_bracket_end_len(equals);
                } else {
                    syntax_mark(visible_offset,
                                styles,
                                visible_len,
                                i,
                                line_len,
                                SOLAR_OS_SYNTAX_STYLE_STRING);
                    state->mode = SOLAR_OS_SYNTAX_MODE_LUA_LONG_STRING;
                    state->lua_long_equals = equals;
                    return;
                }
                continue;
            }
        }

        if (isdigit((unsigned char)ch)) {
            const size_t end = syntax_parse_number(line, line_len, i);
            syntax_mark(visible_offset,
                        styles,
                        visible_len,
                        i,
                        end,
                        SOLAR_OS_SYNTAX_STYLE_NUMBER);
            i = end;
            continue;
        }

        if (syntax_ident_start(ch)) {
            const size_t start = i;
            i++;
            while (i < line_len && syntax_ident(line[i])) {
                i++;
            }
            if (lua_keyword(&line[start], i - start)) {
                syntax_mark(visible_offset,
                            styles,
                            visible_len,
                            start,
                            i,
                            SOLAR_OS_SYNTAX_STYLE_KEYWORD);
            }
            continue;
        }

        i++;
    }
}

void solar_os_syntax_highlight_line(solar_os_syntax_language_t language,
                                    solar_os_syntax_state_t *state,
                                    const char *line,
                                    size_t line_len,
                                    size_t visible_offset,
                                    uint8_t *styles,
                                    size_t visible_len)
{
    if (styles != NULL) {
        memset(styles, SOLAR_OS_SYNTAX_STYLE_NORMAL, visible_len);
    }
    if (state == NULL || line == NULL || language == SOLAR_OS_SYNTAX_NONE) {
        return;
    }

    switch (language) {
    case SOLAR_OS_SYNTAX_PYTHON:
        python_highlight_line(state, line, line_len, visible_offset, styles, visible_len);
        break;
    case SOLAR_OS_SYNTAX_LUA:
        lua_highlight_line(state, line, line_len, visible_offset, styles, visible_len);
        break;
    case SOLAR_OS_SYNTAX_NONE:
    default:
        break;
    }
}

size_t solar_os_syntax_line_count(const char *text, size_t len)
{
    size_t n=1; for(size_t i=0;i<len;++i) if(text[i]=='\n') ++n; return n;
}
void solar_os_syntax_cache_init(solar_os_syntax_cache_t *c,
    solar_os_syntax_checkpoint_t *lines,size_t capacity,const char *text,size_t len)
{
    memset(c,0,sizeof(*c)); c->lines=lines;c->capacity=capacity;
    c->count=solar_os_syntax_line_count(text,len);
    if(!lines || capacity<c->count) {c->count=0;return;}
    memset(lines,0,c->count*sizeof(*lines));lines[0].known=1;
    c->valid=1;c->dirty_through=c->count;
}
size_t solar_os_syntax_cache_edit(solar_os_syntax_cache_t *c,
    const char *text,size_t len,size_t start,size_t removed,const char *inserted,size_t added)
{
    if(start>len || removed>len-start || !c->count)return 0;
    size_t first=solar_os_syntax_line_count(text,start)-1;
    size_t gone=solar_os_syntax_line_count(text+start,removed)-1;
    size_t extra=solar_os_syntax_line_count(inserted,added)-1;
    size_t count=c->count-gone+extra;
    if(count>c->capacity) {c->count=0;c->valid=0;return first;}
    size_t last=first+gone;
    memmove(c->lines+first+extra+1,c->lines+last+1,
        (c->count-last-1)*sizeof(*c->lines));
    memset(c->lines+first+1,0,extra*sizeof(*c->lines));
    size_t dirty=first+extra;
    size_t frontier=c->dirty_through;
    if(c->valid && frontier<c->valid-1)frontier=c->valid-1;
    if(c->valid<c->count && frontier>last)dirty=frontier-gone+extra;
    c->dirty_through=dirty;c->count=count;
    if(c->valid>first+1)c->valid=first+1;
    return first;
}
int solar_os_syntax_cache_state(const solar_os_syntax_cache_t *c,size_t line,solar_os_syntax_state_t *s)
{
    solar_os_syntax_state_init(s);
    if(!s || line>=c->valid || line>=c->count)return 0;
    s->mode=(solar_os_syntax_mode_t)c->lines[line].mode;s->lua_long_equals=c->lines[line].equals;return 1;
}
void solar_os_syntax_cache_step(solar_os_syntax_cache_t *c,solar_os_syntax_language_t language,
    const char *text,size_t len,size_t through,size_t line_budget,size_t byte_budget)
{
    if(!c->count || !c->valid || through<c->valid || !line_budget || !byte_budget)return;
    size_t row=c->valid-1,pos=0;
    for(size_t i=0;i<row && pos<len;++i) {while(pos<len && text[pos]!='\n')++pos;if(pos<len)++pos;}
    size_t bytes=0,work=0;
    while(c->valid<c->count && c->valid<=through && work<line_budget && bytes<byte_budget) {
        size_t end=pos;while(end<len && text[end]!='\n')++end;
        solar_os_syntax_state_t state;solar_os_syntax_cache_state(c,row,&state);
        solar_os_syntax_highlight_line(language,&state,text+pos,end-pos,0,NULL,0);
        ++work;++c->lexed_lines;bytes+=end-pos+1;++row;
        solar_os_syntax_checkpoint_t old=c->lines[row];
        c->lines[row]=(solar_os_syntax_checkpoint_t){(uint8_t)state.mode,state.lua_long_equals,1};
        c->valid=row+1;pos=end<len?end+1:len;
        if(row>c->dirty_through && old.known && old.mode==state.mode && old.equals==state.lua_long_equals) {
            while(c->valid<c->count && c->lines[c->valid].known)++c->valid;
            if(c->valid>row+1)break; /* resume at new frontier on next call */
        }
    }
}
