/*
 * Copyright 2013-2020 Automatak, LLC
 *
 * Licensed to Green Energy Corp (www.greenenergycorp.com) and Automatak
 * LLC (www.automatak.com) under one or more contributor license agreements.
 * See the NOTICE file distributed with this work for additional information
 * regarding copyright ownership. Green Energy Corp and Automatak LLC license
 * this file to you under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License. You may obtain
 * a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* SolarOS adaptation, 2026: bounded passive C decoder. See README.solaros.md. */
#include "dnp3.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint32_t le(const uint8_t *p,unsigned n) {
    uint32_t v=0; for (unsigned i=0;i<n;i++) v|=(uint32_t)p[i]<<(8*i); return v;
}
static uint64_t time48(const uint8_t *p) { return le(p,4) | ((uint64_t)le(p+4,2)<<32); }
static void say(dnp3_decoder_t *d,const char *fmt,...) {
    char text[176]; va_list ap; va_start(ap,fmt); vsnprintf(text,sizeof(text),fmt,ap); va_end(ap);
    if (d->event) d->event(d->user,text,NULL,0);
}
static bool crc_ok(const uint8_t *p,size_t n) { return dnp3_crc(p,n)==le(p+n,2); }
static void drop_flows(dnp3_decoder_t *d) {
    for (unsigned i=0;i<DNP3_FLOWS;i++) d->flows[i].active=d->flows[i].have_last=false;
}
void dnp3_init(dnp3_decoder_t *d,dnp3_event_fn event,void *user) {
    memset(d,0,sizeof(*d)); d->event=event; d->user=user;
}
void dnp3_gap(dnp3_decoder_t *d) {
    if (d->used) { say(d,"truncated link frame (%u bytes)",(unsigned)d->used); d->stats.malformed++; }
    for (unsigned i=0;i<DNP3_FLOWS;i++) if (d->flows[i].active) {
        say(d,"incomplete transport %u>%u",d->flows[i].source,d->flows[i].destination); d->stats.transport_errors++;
    }
    d->used=0; drop_flows(d);
}
void dnp3_expire(dnp3_decoder_t *d,uint32_t now,uint32_t timeout) {
    if (d->used && (uint32_t)(now-d->now)>=timeout) {
        say(d,"link timeout (%u buffered bytes)",(unsigned)d->used); d->used=0; d->stats.malformed++;
    }
    for (unsigned i=0;i<DNP3_FLOWS;i++) if (d->flows[i].active && (uint32_t)(now-d->flows[i].touched)>=timeout) {
        say(d,"transport timeout %u>%u",d->flows[i].source,d->flows[i].destination);
        d->flows[i].active=d->flows[i].have_last=false; d->stats.transport_errors++;
    }
}

enum value_kind { FLAGS, UNSIGNED, SIGNED, FLOATING, CROB, TIME, DELAY, OCTETS };
typedef struct { unsigned size,bits,width,time; bool flags; enum value_kind kind; } layout_t;
static bool layout(unsigned g,unsigned v,layout_t *l) {
    memset(l,0,sizeof(*l));
    if ((g==1 || g==10 || g==80) && v==1) { l->bits=1; return true; }
    if (g==3 && v==1) { l->bits=2; return true; }
    if ((g==1 || g==3 || g==10) && v==2) { l->size=1; return true; }
    if ((g==2 || g==4) && v>=1 && v<=3) { l->size=v==1?1:v==2?7:3; l->time=v==2?6:v==3?2:0; return true; }
    if (g==11 && (v==1 || v==2)) { l->size=v==1?1:7; l->time=v==2?6:0; return true; }
    if (g==12 && v==1) { l->size=11; l->kind=CROB; return true; }
    if ((g==20 || g==22 || g==23) && (v==1 || v==2 || v==5 || v==6)) {
        l->kind=UNSIGNED; l->width=(v==1 || v==5)?4:2; l->flags=g!=20 || v<5;
        l->time=(g!=20 && v>=5)?6:0; l->size=l->width+l->flags+l->time; return true;
    }
    if (g==30 && v>=1 && v<=6) {
        l->kind=v>=5?FLOATING:SIGNED; l->width=v==2 || v==4?2:v==6?8:4;
        l->flags=v!=3 && v!=4; l->size=l->width+l->flags; return true;
    }
    if (g==32 && v>=1 && v<=8) {
        l->kind=v>=5?FLOATING:SIGNED; l->width=v==2 || v==4?2:v==6 || v==8?8:4;
        l->flags=true; l->time=v==3 || v==4 || v==7 || v==8?6:0; l->size=1+l->width+l->time; return true;
    }
    if (g==40 && v>=1 && v<=4) {
        l->kind=v>=3?FLOATING:SIGNED; l->width=v==2?2:v==4?8:4;
        l->flags=true; l->size=1+l->width; return true;
    }
    if (g==41 && v>=1 && v<=4) {
        l->kind=v>=3?FLOATING:SIGNED; l->width=v==2?2:v==4?8:4;
        l->size=1+l->width; return true; /* status follows value */
    }
    if ((g==50 && v==1) || (g==51 && (v==1 || v==2))) { l->size=6; l->kind=TIME; return true; }
    if (g==52 && (v==1 || v==2)) { l->size=2; l->kind=DELAY; return true; }
    if ((g==110 || g==111) && v) { l->size=v; l->kind=OCTETS; return true; }
    return false;
}
static void value(dnp3_decoder_t *d,unsigned g,unsigned v,uint32_t index,const uint8_t *p,const layout_t *l) {
    char text[96],extra[48]="";
    const uint8_t *number=p+(l->flags?1:0);
    if (l->kind==FLAGS) {
        unsigned state=(g==3 || g==4)?(p[0]>>6):(p[0]>>7);
        snprintf(text,sizeof(text),"state=%u flags=%02X",state,p[0]);
    } else if (l->kind==SIGNED) {
        int32_t n=l->width==2?(int16_t)le(number,2):(int32_t)le(number,4);
        snprintf(text,sizeof(text),"value=%ld",(long)n);
    } else if (l->kind==UNSIGNED) snprintf(text,sizeof(text),"value=%lu",(unsigned long)le(number,l->width));
    else if (l->kind==FLOATING) {
        double n;
        if (l->width==4) { uint32_t u=le(number,4); float f; memcpy(&f,&u,4); n=f; }
        else { uint64_t u=le(number,4)|((uint64_t)le(number+4,4)<<32); memcpy(&n,&u,8); }
        snprintf(text,sizeof(text),"value=%.9g",n);
    } else if (l->kind==CROB) {
        snprintf(text,sizeof(text),"control=%02X count=%u on=%lu off=%lu status=%u",p[0],p[1],(unsigned long)le(p+2,4),(unsigned long)le(p+6,4),p[10]);
    } else if (l->kind==TIME) snprintf(text,sizeof(text),"time_ms=%llu",(unsigned long long)time48(p));
    else if (l->kind==DELAY) snprintf(text,sizeof(text),"delay=%lu %s",(unsigned long)le(p,2),v==1?"seconds":"ms");
    else {
        size_t n=l->size<16?l->size:16;
        size_t pos=(size_t)snprintf(text,sizeof(text),"octets(%u)=",l->size);
        for (size_t i=0;i<n;i++) pos+=(size_t)snprintf(text+pos,sizeof(text)-pos,"%02X",p[i]);
        if (l->size>n) snprintf(text+pos,sizeof(text)-pos,"...");
    }
    if (l->flags) snprintf(extra,sizeof(extra)," flags=%02X",p[0]);
    if (g==41) snprintf(extra,sizeof(extra)," status=%u",p[l->width]);
    say(d,"  g%uv%u[%lu] %s%s",g,v,(unsigned long)index,text,extra);
    if (l->time==6) say(d,"    time_ms=%llu",(unsigned long long)time48(p+l->size-6));
    else if (l->time==2) say(d,"    relative_ms=%lu",(unsigned long)le(p+l->size-2,2));
}
static void objects(dnp3_decoder_t *d,const uint8_t *p,size_t n,bool selection) {
    unsigned printed=0;
    while (n) {
        if (n<3) goto malformed;
        unsigned g=p[0],v=p[1],q=p[2],prefix=q>>4,range=q&15;
        p+=3; n-=3;
        uint32_t first=0,count=0;
        if (q&128 || prefix>3) goto unsupported;
        if (range<=2) {
            unsigned w=1U<<range;
            if (prefix || n<2*w) goto malformed;
            first=le(p,w); uint32_t last=le(p+w,w); p+=2*w; n-=2*w;
            if (last<first || (uint64_t)last-first+1>UINT32_MAX) goto malformed;
            count=last-first+1;
        } else if (range>=7 && range<=9) {
            unsigned w=1U<<(range-7); if (n<w) goto malformed;
            count=le(p,w); p+=w; n-=w;
        } else if (range!=6 || prefix) goto unsupported;
        say(d,"OBJ g%uv%u q=%02X %s%lu start=%lu",g,v,q,range==6?"all count=":"count=",(unsigned long)count,(unsigned long)first);
        if (selection) {
            if (prefix) goto unsupported;
            continue;
        }
        if (range==6) goto unsupported; /* no count: never guess a payload boundary */
        layout_t l;
        if (!layout(g,v,&l)) goto unsupported;
        if (l.bits) {
            if (prefix) goto unsupported;
            uint64_t bytes=((uint64_t)count*l.bits+7)/8;
            if (bytes>n) goto malformed;
            for (uint32_t i=0;i<count && printed<64;i++,printed++)
                say(d,"  g%uv%u[%lu] state=%u",g,v,(unsigned long)(first+i),(p[(i*l.bits)/8]>>((i*l.bits)%8))&((1U<<l.bits)-1));
            p+=(size_t)bytes; n-=(size_t)bytes;
        } else {
            unsigned iw=prefix?1U<<(prefix-1):0;
            if ((uint64_t)count*(l.size+iw)>n) goto malformed;
            for (uint32_t i=0;i<count;i++) {
                uint32_t index=iw?le(p,iw):first+i; p+=iw; n-=iw;
                if (printed<64) { value(d,g,v,index,p,&l); printed++; }
                p+=l.size; n-=l.size;
            }
        }
        if (printed>=64) say(d,"value display capped at 64; remaining values validated/skipped");
        continue;
unsupported:
        d->stats.unsupported++; say(d,"unsupported object/qualifier g%uv%u q=%02X; %u payload bytes left raw",g,v,q,(unsigned)n); return;
    }
    return;
malformed:
    d->stats.malformed++; say(d,"truncated/invalid object data (%u bytes remain)",(unsigned)n);
}
static void application(dnp3_decoder_t *d,const uint8_t *p,size_t n) {
    if (n<2) { d->stats.malformed++; say(d,"short application header"); return; }
    uint8_t control=p[0],function=p[1];
    say(d,"APP %s(0x%02X) seq=%u FIR=%u FIN=%u CON=%u UNS=%u len=%u",dnp3_function_name(function),function,control&15,!!(control&128),!!(control&64),!!(control&32),!!(control&16),(unsigned)n);
    p+=2; n-=2;
    if (function==129 || function==130) {
        if (n<2) { d->stats.malformed++; say(d,"short response IIN"); return; }
        unsigned iin=le(p,2); p+=2; n-=2;
        say(d,"IIN=%04X%s%s%s%s%s%s",iin,(iin&128)?" RESTART":"",(iin&16)?" NEED_TIME":"",(iin&0x200)?" UNKNOWN_OBJECT":"",(iin&0x400)?" PARAM_ERROR":"",(iin&0x800)?" EVENT_OVERFLOW":"",(iin&0x1000)?" ALREADY_EXECUTING":"");
    }
    bool selection=function==1 || (function>=7 && function<=10) || function==20 || function==21 || function==22;
    if (!selection && function!=2 && function!=3 && function!=4 && function!=5 && function!=6 && function!=129 && function!=130) {
        if (n) { d->stats.unsupported++; say(d,"function payload left raw (%u bytes)",(unsigned)n); }
        return;
    }
    objects(d,p,n,selection);
}
static void transport(dnp3_decoder_t *d,uint16_t source,uint16_t destination,const uint8_t *p,size_t n) {
    if (!n) return;
    uint8_t header=*p++,seq=header&63; n--;
    bool fir=(header&64)!=0,fin=(header&128)!=0;
    dnp3_flow_t *f=NULL,*free_flow=NULL,*oldest=&d->flows[0];
    for (unsigned i=0;i<DNP3_FLOWS;i++) {
        dnp3_flow_t *x=&d->flows[i];
        if (x->active && x->source==source && x->destination==destination) f=x;
        if (!x->active) free_flow=x;
        if ((uint32_t)(d->now-x->touched)>(uint32_t)(d->now-oldest->touched)) oldest=x;
    }
    say(d,"TP seq=%u FIR=%u FIN=%u bytes=%u",seq,fir,fin,(unsigned)n);
    if (f && f->have_last && seq==f->last_sequence && n==f->last_length && !memcmp(p,f->last,n)) {
        say(d,"duplicate transport segment (not appended)"); f->touched=d->now; return;
    }
    if (fir) {
        if (!f) f=free_flow?free_flow:oldest;
        if (f->active) { d->stats.transport_errors++; say(d,"incomplete transport replaced"); }
        memset(f,0,sizeof(*f)); f->active=true; f->source=source; f->destination=destination;
    } else if (!f || seq!=f->expected) {
        d->stats.transport_errors++; say(d,"transport gap: need FIR or expected sequence");
        if (f) f->active=f->have_last=false;
        return;
    }
    if (n>(size_t)(DNP3_FRAGMENT_MAX-f->used)) {
        d->stats.transport_errors++; say(d,"transport fragment exceeds %u bytes",DNP3_FRAGMENT_MAX); f->active=false; return;
    }
    memcpy(f->data+f->used,p,n); f->used+=n; f->expected=(seq+1)&63; f->touched=d->now;
    memcpy(f->last,p,n); f->last_length=n; f->last_sequence=seq; f->have_last=true;
    if (fin) { application(d,f->data,f->used); f->active=false; }
}
static const char *link_name(uint8_t control) {
    unsigned fn=control&15;
    if (control&64) switch(fn) {
        case 0:return "RESET"; case 2:return "TEST"; case 3:return "CONFIRMED_DATA";
        case 4:return "UNCONFIRMED_DATA"; case 9:return "REQUEST_STATUS";
    }
    else switch(fn) { case 0:return "ACK"; case 1:return "NACK"; case 11:return "STATUS"; case 15:return "NOT_SUPPORTED"; }
    return "UNKNOWN";
}
static void shift(dnp3_decoder_t *d,size_t n) { d->used-=n; memmove(d->frame,d->frame+n,d->used); }
static void parse(dnp3_decoder_t *d) {
    while (d->used>=2) {
        if (d->frame[0]!=5 || d->frame[1]!=100) { shift(d,1); d->stats.discarded++; continue; }
        if (d->used<10) return;
        if (!crc_ok(d->frame,8)) {
            d->stats.header_crc++; if(d->event) d->event(d->user,"CRC ERROR header",d->frame,10);
            drop_flows(d); shift(d,1); continue;
        }
        if (d->frame[2]<5) { d->stats.malformed++; say(d,"invalid link length=%u",d->frame[2]); drop_flows(d); shift(d,1); continue; }
        size_t payload=d->frame[2]-5,frame_size=10+payload+2*((payload+15)/16);
        if (d->used<frame_size) return;
        uint8_t body[250]; size_t offset=10,done=0; bool valid=true;
        while (done<payload) {
            size_t size=payload-done; if (size>16) size=16;
            if (!crc_ok(d->frame+offset,size)) valid=false;
            memcpy(body+done,d->frame+offset,size); offset+=size+2; done+=size;
        }
        uint8_t control=d->frame[3]; uint16_t destination=le(d->frame+4,2),source=le(d->frame+6,2);
        char text[176];
        snprintf(text,sizeof(text),"LINK %u>%u %s DIR=%u PRM=%u FCB=%u FCV/DFC=%u bytes=%u CRC=%s",source,destination,link_name(control),!!(control&128),!!(control&64),!!(control&32),!!(control&16),(unsigned)frame_size,valid?"OK":"BAD");
        if (d->event) d->event(d->user,text,d->frame,frame_size);
        if (valid) {
            d->stats.frames++;
            if ((control&64) && ((control&15)==3 || (control&15)==4)) transport(d,source,destination,body,payload);
            shift(d,frame_size);
        } else { d->stats.body_crc++; drop_flows(d); shift(d,1); }
    }
}
void dnp3_feed(dnp3_decoder_t *d,const uint8_t *data,size_t length,uint32_t now) {
    if (!d || (!data && length)) return;
    d->now=now;
    for (size_t i=0;i<length;i++) {
        if (d->used==DNP3_FRAME_MAX) { d->stats.malformed++; shift(d,1); }
        d->frame[d->used++]=data[i]; d->stats.bytes++; parse(d);
    }
}
