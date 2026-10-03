#pragma once
#include <stddef.h>
#include <string.h>
#include <ctype.h>

namespace skshell {
enum Link { End, Always, Success, Pipe };
struct Step { size_t begin, end; Link next; };
struct Plan { Step steps[8]{}; unsigned count=0; bool composed=false; const char *error=nullptr; };
// Preserve quoting for the existing tokenizer. Validate the entire line before
// executing anything; separators inside quotes or escaped separators are data.
inline Plan plan(const char *s) {
    Plan p; size_t begin=0; char quote=0;
    if (!s || strlen(s)>191) {p.error="maximum command line is 191 bytes";return p;}
    for(size_t i=0;;++i) {
        const char c=s[i];
        if(c=='\\' && quote!='\'') {
            if(!s[i+1]){p.error="trailing backslash";return p;}
            ++i;continue;
        }
        if(c && (c=='\'' || c=='"')) {
            if(!quote)quote=c;else if(quote==c)quote=0;
            continue;
        }
        if(!c && quote){p.error="unterminated quote";return p;}
        if(quote)continue;
        if(c && c!=';' && c!='&' && c!='|' && c!='<' && c!='>')continue;
        Link link=End; size_t width=1;
        if(c) {
            p.composed=true;
            if(c==';')link=Always;
            else if(c=='&' && s[i+1]=='&'){link=Success;width=2;}
            else if(c=='|' && s[i+1]!='|')link=Pipe;
            else {p.error="supported operators are ; && and |";return p;}
        }
        size_t a=begin,b=i;
        while(a<b && isspace((unsigned char)s[a]))++a;
        // Do not trim the end: its whitespace may be backslash-escaped.
        if(a==b) {
            if(!c && (!p.count || p.steps[p.count-1].next==Always))return p;
            p.error="missing command beside operator";return p;
        }
        if(p.count==8){p.error="maximum is 8 commands per line";return p;}
        p.steps[p.count++]={a,b,link};
        if(!c)return p;
        i+=width-1;begin=i+1;
    }
}
}
