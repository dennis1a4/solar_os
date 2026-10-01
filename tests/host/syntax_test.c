#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "solar_os_syntax.h"
static void token(const char *line,const char *word,unsigned style) {
    unsigned char colors[256];solar_os_syntax_state_t s;solar_os_syntax_state_init(&s);
    solar_os_syntax_highlight_line(SOLAR_OS_SYNTAX_PYTHON,&s,line,strlen(line),0,colors,strlen(line));
    size_t start=(size_t)(strstr(line,word)-line);for(size_t i=0;i<strlen(word);++i)assert(colors[start+i]==style);
}
static unsigned iteration;
static void verify(solar_os_syntax_cache_t *c,const char *text) {
    solar_os_syntax_state_t full;solar_os_syntax_state_init(&full);size_t row=0,pos=0,len=strlen(text);
    for(;;){solar_os_syntax_state_t cached;if(solar_os_syntax_cache_state(c,row,&cached)) {
        if(cached.mode!=full.mode || cached.lua_long_equals!=full.lua_long_equals) {fprintf(stderr,"iteration=%u row=%zu valid=%zu dirty=%zu cached=%d full=%d text=[%s]\n",iteration,row,c->valid,c->dirty_through,cached.mode,full.mode,text);abort();}
    }
    size_t end=pos;while(end<len && text[end]!='\n')++end;
    solar_os_syntax_highlight_line(SOLAR_OS_SYNTAX_PYTHON,&full,text+pos,end-pos,0,NULL,0);
    if(end==len)break;
    pos=end+1;++row;}
    assert(row+1==c->count);
}
int main(void) {
    token("if x: print(len(range(4)))", "if",SOLAR_OS_SYNTAX_STYLE_KEYWORD);
    token("if x: print(len(range(4)))", "print",SOLAR_OS_SYNTAX_STYLE_BUILTIN);
    token("obj.print(1)","print",SOLAR_OS_SYNTAX_STYLE_NORMAL);
    token("async def print(x):", "print",SOLAR_OS_SYNTAX_STYLE_DEFINITION);
    token("class Widget:","Widget",SOLAR_OS_SYNTAX_STYLE_DEFINITION);
    token("return True, False, None", "None",SOLAR_OS_SYNTAX_STYLE_CONSTANT);
    token("x = 0xff + .5e-2 # comment", ".5e-2",SOLAR_OS_SYNTAX_STYLE_NUMBER);
    token("x = 0xff + .5e-2 # comment", "# comment",SOLAR_OS_SYNTAX_STYLE_COMMENT);
    token("x = r'print # if'", "r'print # if'",SOLAR_OS_SYNTAX_STYLE_STRING);
    char text[16384]="def f():\n    text = '''hello\nworld\n'''\n    return True\n";
    solar_os_syntax_checkpoint_t states[8192];solar_os_syntax_cache_t c;
    solar_os_syntax_cache_init(&c,states,8192,text,strlen(text));
    while(c.valid<c.count)solar_os_syntax_cache_step(&c,SOLAR_OS_SYNTAX_PYTHON,text,strlen(text),c.count,2,32);
    verify(&c,text);size_t work=c.lexed_lines;
    for(int i=0;i<100;++i)solar_os_syntax_cache_step(&c,SOLAR_OS_SYNTAX_PYTHON,text,strlen(text),c.count,64,8192);
    assert(work==c.lexed_lines); /* No lexing on cursor-only redraw. */
    srand(1234);
    const char *inserts[]={"x","\n","'''","#","\"\"\""," ","\\",""};
    for(unsigned edit=0;edit<4000;++edit) {
        iteration=edit;
        size_t len=strlen(text),at=(unsigned)rand()%(len+1),gone=(unsigned)rand()%5;
        if(gone>len-at)gone=len-at;
        const char *add=inserts[(unsigned)rand()%8];size_t added=strlen(add);
        solar_os_syntax_cache_edit(&c,text,len,at,gone,add,added);
        memmove(text+at+added,text+at+gone,len-at-gone+1);memcpy(text+at,add,added);
        /* Include multiple edits before propagation finishes. */
        solar_os_syntax_cache_step(&c,SOLAR_OS_SYNTAX_PYTHON,text,strlen(text),c.count,1+(unsigned)rand()%3,20);
        verify(&c,text);
        if(edit%7==0) {while(c.valid<c.count)solar_os_syntax_cache_step(&c,SOLAR_OS_SYNTAX_PYTHON,text,strlen(text),c.count,3,32);verify(&c,text);}
    }
    /* Large cached files converge after one unchanged outgoing line state. */
    text[0]=0;for(int i=0;i<1800;++i)strcat(text,"x = 1\n");
    solar_os_syntax_cache_init(&c,states,8192,text,strlen(text));
    while(c.valid<c.count) {
        size_t before=c.lexed_lines;
        solar_os_syntax_cache_step(&c,SOLAR_OS_SYNTAX_PYTHON,text,strlen(text),c.count,64,8192);
        assert(c.lexed_lines-before<=64);
    }
    work=c.lexed_lines;solar_os_syntax_cache_edit(&c,text,strlen(text),0,1,"y",1);text[0]='y';
    solar_os_syntax_cache_step(&c,SOLAR_OS_SYNTAX_PYTHON,text,strlen(text),c.count,64,8192);
    assert(c.valid==c.count && c.lexed_lines==work+1);verify(&c,text);
    puts("PASS: Python token styles, cached-state equivalence across 4000 edits, bounded propagation and no cursor-only re-lexing");
}
