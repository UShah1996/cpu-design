/*
 * ============================================================
 * CMPE-220 Software CPU — Assembler Implementation (C99)
 * ============================================================
 * THE BRANCH LABEL BUG FIX:
 *
 * PROBLEM: src field is only 6 bits (max value = 63 = 0x3F).
 *   Code starts at 0x0100. So labels like FIB_LOOP (0x0105),
 *   FIB_DONE (0x010F) have addresses that CANNOT fit in 6 bits.
 *   Old code did:  src = label_addr & 0x3F  → WRONG address!
 *
 * FIX: Constant Pool pattern (same as ARM Thumb literal pools).
 *   Pass 1: when label addr > 63, allocate pool slot N,
 *           store address in pool[N].
 *   Pass 2: encode branch as DIRECT mode, src = N.
 *   Runtime: CPU reads mem[N] = full address → correct jump.
 *
 *   Example:
 *     FIB_DONE = 0x010F  (doesn't fit in 6 bits)
 *     Pool slot 2 → mem[2] = 0x010F
 *     Encoded:  BEQ [02]  (DIRECT mode, src=2, fits in 6 bits)
 *     CPU runs: src_val = mem[2] = 0x010F → PC = 0x010F ✓
 * ============================================================
 */

#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Token list for one line ─────────────────────────────────*/
typedef struct {
    char tokens[MAX_TOKENS][MAX_LINE_LEN];
    int  count;
} TokenLine;

/* ── Pool slot: maps an address to a memory slot 0x00-0x0F ──*/
typedef struct { char name[MAX_LABEL_LEN]; int slot; Word value; } PoolSlot;
typedef struct { PoolSlot entries[MAX_POOL_SLOTS]; int count; int next_free; } PoolTable;

/* ─────────────────────────────────────────────────────────── */
/* HELPERS                                                      */
/* ─────────────────────────────────────────────────────────── */

static void str_toupper(char* s) {
    while (*s) { *s=(char)toupper((unsigned char)*s); s++; }
}
static void str_trim(char* s) {
    int st=0, en=(int)strlen(s)-1;
    while(s[st]==' '||s[st]=='\t') st++;
    while(en>=st&&(s[en]==' '||s[en]=='\t'||s[en]=='\r'||s[en]=='\n')) en--;
    memmove(s,s+st,en-st+1); s[en-st+1]='\0';
}
static int is_label_def(const char*s){int l=(int)strlen(s);return l>1&&s[l-1]==':';}
static int is_directive(const char*s){return s&&s[0]=='.';}

static TokenLine tokenize_line(const char* line) {
    TokenLine tl; char buf[MAX_LINE_LEN]; int ti=0; char*p;
    tl.count=0;
    strncpy(buf,line,MAX_LINE_LEN-1); buf[MAX_LINE_LEN-1]='\0';
    {char*sc=strchr(buf,';');if(sc)*sc='\0';}
    p=buf;
    while(*p&&ti<MAX_TOKENS){
        int i=0;
        while(*p==' '||*p=='\t'||*p==',') p++;
        if(!*p) break;
        while(*p&&*p!=' '&&*p!='\t'&&*p!=',') tl.tokens[ti][i++]=*p++;
        tl.tokens[ti][i]='\0';
        if(i>0){str_toupper(tl.tokens[ti]);ti++;tl.count=ti;}
    }
    return tl;
}

/* ─────────────────────────────────────────────────────────── */
/* LABEL TABLE                                                  */
/* ─────────────────────────────────────────────────────────── */

static void label_add(LabelTable*lt,const char*name,Address addr){
    size_t len; if(lt->count>=MAX_LABELS)return;
    len=strlen(name); if(len>MAX_LABEL_LEN-1)len=MAX_LABEL_LEN-1;
    memcpy(lt->entries[lt->count].name,name,len);
    lt->entries[lt->count].name[len]='\0';
    lt->entries[lt->count].addr=addr; lt->count++;
}
static Address label_find(const LabelTable*lt,const char*name){
    int i; for(i=0;i<lt->count;i++) if(!strcmp(lt->entries[i].name,name)) return lt->entries[i].addr;
    return 0xFFFF;
}

/* ─────────────────────────────────────────────────────────── */
/* POOL TABLE                                                   */
/* ─────────────────────────────────────────────────────────── */

static int pool_alloc(PoolTable*pt,const char*name,Word addr){
    int i;
    for(i=0;i<pt->count;i++) if(pt->entries[i].value==addr) return pt->entries[i].slot;
    if(pt->count>=MAX_POOL_SLOTS||pt->next_free>=16) return -1;
    i=pt->count++;
    pt->entries[i].slot=pt->next_free++;
    pt->entries[i].value=addr;
    memcpy(pt->entries[i].name,name,MAX_LABEL_LEN-1); pt->entries[i].name[MAX_LABEL_LEN-1] = 0;
    pt->entries[i].name[MAX_LABEL_LEN-1]='\0';
    return pt->entries[i].slot;
}
static int pool_find(const PoolTable*pt,Word addr){
    int i; for(i=0;i<pt->count;i++) if(pt->entries[i].value==addr) return pt->entries[i].slot;
    return -1;
}

/* ─────────────────────────────────────────────────────────── */
/* TOKEN RESOLUTION                                             */
/* ─────────────────────────────────────────────────────────── */

static int resolve_token(const char*tok,const LabelTable*lt,int*ok){
    *ok=1;
    if(!tok||!tok[0]){*ok=0;return 0;}
    if(tok[0]=='#') return resolve_token(tok+1,lt,ok);
    if(tok[0]=='['){
        int len=(int)strlen(tok);
        if(tok[len-1]==']'){char in[MAX_LINE_LEN];strncpy(in,tok+1,len-2);in[len-2]='\0';return resolve_token(in,lt,ok);}
    }
    if(tok[0]=='0'&&(tok[1]=='X'||tok[1]=='x')) return (int)strtoul(tok+2,NULL,16);
    if(isdigit((unsigned char)tok[0])||(tok[0]=='-'&&isdigit((unsigned char)tok[1]))) return atoi(tok);
    if(tok[0]=='R'&&isdigit((unsigned char)tok[1])) return atoi(tok+1);
    if(!strcmp(tok,"SP")) return REG_SP;
    if(!strcmp(tok,"LR")) return REG_LR;
    if(!strcmp(tok,"PC")) return REG_PC;
    {Address a=label_find(lt,tok);if(a!=0xFFFF)return (int)a;}
    *ok=0; return 0;
}

static AddrMode parse_mode(const char*tok,const LabelTable*lt){
    if(!tok||!tok[0]) return MODE_REG;
    if(tok[0]=='['){
        int len=(int)strlen(tok);
        if(len>2&&tok[len-1]==']'){
            char in[MAX_LINE_LEN]; strncpy(in,tok+1,len-2); in[len-2]='\0';
            if((in[0]=='R'&&isdigit((unsigned char)in[1]))||!strcmp(in,"SP")||!strcmp(in,"LR")||!strcmp(in,"PC"))
                return MODE_INDIRECT;
            return MODE_DIRECT;
        }
    }
    if(tok[0]=='#') return MODE_IMM;
    if((tok[0]=='R'&&isdigit((unsigned char)tok[1]))||!strcmp(tok,"SP")||!strcmp(tok,"LR")||!strcmp(tok,"PC"))
        return MODE_REG;
    (void)lt; return MODE_IMM;
}

static int mnemonic_to_opcode(const char*m,Opcode*op){
    if(!strcmp(m,"ADD")){*op=OP_ADD;return 1;} if(!strcmp(m,"SUB")){*op=OP_SUB;return 1;}
    if(!strcmp(m,"AND")){*op=OP_AND;return 1;} if(!strcmp(m,"OR")) {*op=OP_OR; return 1;}
    if(!strcmp(m,"NOT")){*op=OP_NOT;return 1;} if(!strcmp(m,"SHL")){*op=OP_SHL;return 1;}
    if(!strcmp(m,"SHR")){*op=OP_SHR;return 1;} if(!strcmp(m,"SAR")){*op=OP_SAR;return 1;}
    if(!strcmp(m,"LOAD")) {*op=OP_LOAD; return 1;} if(!strcmp(m,"STORE")){*op=OP_STORE;return 1;}
    if(!strcmp(m,"JMP"))  {*op=OP_JMP;  return 1;} if(!strcmp(m,"BEQ"))  {*op=OP_BEQ;  return 1;}
    if(!strcmp(m,"BNE"))  {*op=OP_BNE;  return 1;} if(!strcmp(m,"BLT"))  {*op=OP_BLT;  return 1;}
    if(!strcmp(m,"CALL")) {*op=OP_CALL; return 1;} if(!strcmp(m,"RET"))  {*op=OP_RET;  return 1;}
    return 0;
}

/* ─────────────────────────────────────────────────────────── */
/* ENCODE ONE INSTRUCTION                                       */
/* ─────────────────────────────────────────────────────────── */

static Word encode_from_tokens(const TokenLine*tl,const LabelTable*lt,
                                const PoolTable*pt,Address cur_addr,int*err){
    Opcode op; int dest=0,src=0,ok=1; AddrMode mode=MODE_IMM;
    *err=0;
    if(tl->count<1) return 0;
    if(!mnemonic_to_opcode(tl->tokens[0],&op)){*err=1;return 0;}
    if(op==OP_RET) return encode_instr(op,0,MODE_REG,0);
    if(op==OP_NOT&&tl->count>=2){
        dest=resolve_token(tl->tokens[1],lt,&ok)&0xF;
        return encode_instr(op,dest,MODE_REG,0);
    }

    /* ── BRANCH/JUMP/CALL: single target operand ─────────────
     * THE FIX: if target addr > 63, use pool slot via DIRECT mode
     */
    if(op==OP_JMP||op==OP_BEQ||op==OP_BNE||op==OP_BLT||op==OP_CALL){
        dest=0;
        if(tl->count>=2){
            int target=resolve_token(tl->tokens[1],lt,&ok);
            if(!ok){*err=1;return 0;}
            if(target<=0x3F){
                /* Fits in 6 bits — encode directly */
                mode=parse_mode(tl->tokens[1],lt);
                src=target&0x3F;
            } else {
                /* Too large — look up pool slot allocated in Pass 1 */
                int slot=pool_find(pt,(Word)target);
                if(slot<0){*err=1;return 0;} /* pool slot missing */
                mode=MODE_DIRECT;   /* read full addr from pool */
                src=slot&0x3F;      /* slot number fits in 6b   */
            }
        }
        return encode_instr(op,dest,mode,src);
    }

    /* ── ALU / LOAD / STORE: dest, src ──────────────────────*/
    if(tl->count>=2){dest=resolve_token(tl->tokens[1],lt,&ok)&0xF;if(!ok){*err=1;return 0;}}
    if(tl->count>=3){mode=parse_mode(tl->tokens[2],lt);src=resolve_token(tl->tokens[2],lt,&ok)&0x3F;if(!ok){*err=1;return 0;}}
    (void)cur_addr;
    return encode_instr(op,dest,mode,src);
}

/* ─────────────────────────────────────────────────────────── */
/* MAIN TWO-PASS ASSEMBLE                                       */
/* ─────────────────────────────────────────────────────────── */

AsmProgramEx asm_assemble_ex(const char*source,Address load_addr,int pool_slots_used){
    AsmProgramEx ex; LabelTable labels; PoolTable pool;
    Word words[MAX_PROGRAM]; int word_count=0;
    char lines[MAX_SOURCE_LINES][MAX_LINE_LEN]; int num_lines=0,i,ok_flag=1;
    char err_msg[256]="";

    memset(&ex,0,sizeof(ex)); memset(&labels,0,sizeof(labels));
    memset(&pool,0,sizeof(pool)); memset(words,0,sizeof(words));
    pool.next_free=pool_slots_used;
    ex.base.load_addr=load_addr; ex.base.ok=1;

    /* Split into lines */
    {const char*p=source;int li=0,ci=0;
     while(*p&&num_lines<MAX_SOURCE_LINES){
         if(*p=='\n'){lines[li][ci]='\0';str_trim(lines[li]);li++;ci=0;}
         else if(ci<MAX_LINE_LEN-1) lines[li][ci++]=*p;
         p++;
     }
     if(ci>0){lines[li][ci]='\0';str_trim(lines[li]);li++;}
     num_lines=li;
    }

    /* ── PASS 1: Labels + pool pre-allocation ────────────────*/
    {Address addr=load_addr;
     for(i=0;i<num_lines;i++){
         TokenLine tl; int ti=0;
         if(!lines[i][0]) continue;
         tl=tokenize_line(lines[i]);
         if(tl.count==0) continue;
         if(is_label_def(tl.tokens[ti])){
             char ln[MAX_LABEL_LEN]; int ll=(int)strlen(tl.tokens[ti])-1;
             if(ll>MAX_LABEL_LEN-1)ll=MAX_LABEL_LEN-1;
             memcpy(ln,tl.tokens[ti],ll); ln[ll]='\0';
             label_add(&labels,ln,addr);
             /* *** KEY FIX: pre-allocate pool if addr > 63 *** */
             if(addr>0x3F) pool_alloc(&pool,ln,addr);
             ti++; if(ti>=tl.count) continue;
         }
         if(is_directive(tl.tokens[ti])){
             if(!strcmp(tl.tokens[ti],".ORG")&&tl.count>ti+1){int ok2;addr=(Address)resolve_token(tl.tokens[ti+1],&labels,&ok2);}
             if(!strcmp(tl.tokens[ti],".DATA")) addr++;
             continue;
         }
         if(tl.count>ti) addr++;
     }
    }

    /* ── PASS 2: Encode ──────────────────────────────────────*/
    {Address addr=load_addr;
     for(i=0;i<num_lines&&ok_flag;i++){
         TokenLine tl; int ti=0;
         if(!lines[i][0]) continue;
         tl=tokenize_line(lines[i]); if(tl.count==0) continue;
         if(is_label_def(tl.tokens[ti])){ti++;if(ti>=tl.count)continue;}
         if(is_directive(tl.tokens[ti])){
             if(!strcmp(tl.tokens[ti],".ORG")&&tl.count>ti+1){int ok2;addr=(Address)resolve_token(tl.tokens[ti+1],&labels,&ok2);}
             if(!strcmp(tl.tokens[ti],".DATA")&&tl.count>ti+1){
                 int ok2; Word w=(Word)resolve_token(tl.tokens[ti+1],&labels,&ok2);
                 if(word_count<MAX_PROGRAM){words[word_count++]=w;} addr++;
             }
             continue;
         }
         if(tl.count>ti){
             TokenLine sh=tl; int j,err=0; Word w;
             if(ti>0){for(j=0;j<tl.count-ti;j++)memcpy(sh.tokens[j],tl.tokens[j+ti],MAX_LINE_LEN);sh.count=tl.count-ti;}
             w=encode_from_tokens(&sh,&labels,&pool,addr,&err);
             if(err){
                 ok_flag=0;
                 {char t64[64];strncpy(t64,sh.tokens[0],63);t64[63]='\0';
                  snprintf(err_msg,sizeof(err_msg),"Line %d: unresolved '%s'",i+1,t64);}
                 break;
             }
             if(word_count<MAX_PROGRAM)words[word_count++]=w;
             addr++;
         }
     }
    }

    /* Fill output */
    memcpy(ex.base.words,words,word_count*sizeof(Word));
    ex.base.count=word_count; ex.base.labels=labels; ex.base.ok=ok_flag;
    if(!ok_flag){memcpy(ex.base.error,err_msg,sizeof(ex.base.error)-1); ex.base.error[sizeof(ex.base.error)-1] = 0;}
    for(i=0;i<pool.count;i++){ex.pool_entries[i].slot=pool.entries[i].slot;ex.pool_entries[i].value=pool.entries[i].value;}
    ex.pool_count=pool.count;
    return ex;
}

AsmProgram asm_assemble(const char*source,Address load_addr){
    AsmProgramEx ex=asm_assemble_ex(source,load_addr,0);
    return ex.base;
}

void asm_load_into_memory(const AsmProgram*prog,Memory*mem){
    int i; for(i=0;i<prog->count;i++) mem->ram[prog->load_addr+i]=prog->words[i];
}

void asm_apply_pool_ex(const AsmProgramEx*ex,Memory*mem){
    int i;
    for(i=0;i<ex->pool_count;i++){
        int slot=ex->pool_entries[i].slot;
        Word val=ex->pool_entries[i].value;
        if(slot>=0&&slot<16) mem->ram[slot]=val;
    }
}

void asm_apply_pool(const AsmProgram*prog,Memory*mem){(void)prog;(void)mem;}

void asm_print_listing(const AsmProgram*prog){
    int i; Address a=prog->load_addr;
    printf("\n+============================================+\n");
    printf("|         ASSEMBLED LISTING                  |\n");
    printf("+----------+-------+------------------------+\n");
    printf("|  ADDRESS |  HEX  |  DISASSEMBLY           |\n");
    printf("+----------+-------+------------------------+\n");
    for(i=0;i<prog->count;i++){
        Word w=prog->words[i]; char dis[48];
        Opcode op=decode_opcode(w); int dest=decode_dest(w);
        AddrMode mode=decode_mode(w); int src=decode_src(w);
        switch(mode){
            case MODE_REG:      snprintf(dis,sizeof(dis),"%-5s R%d, R%d",  opcode_name(op),dest,src&0xF);break;
            case MODE_IMM:      snprintf(dis,sizeof(dis),"%-5s R%d, #%d",  opcode_name(op),dest,src);    break;
            case MODE_DIRECT:   snprintf(dis,sizeof(dis),"%-5s R%d, [%02X]",opcode_name(op),dest,src);   break;
            case MODE_INDIRECT: snprintf(dis,sizeof(dis),"%-5s R%d, [R%d]",opcode_name(op),dest,src&0xF);break;
            default:            snprintf(dis,sizeof(dis),"???");break;
        }
        printf("|  %04X    | %04X | %-23s|\n",a,w,dis); a++;
    }
    printf("+----------+-------+------------------------+\n");
    if(prog->labels.count>0){
        int j; printf("|  Labels:                                   |\n");
        for(j=0;j<prog->labels.count;j++)
            printf("|    %-20s -> %04X         |\n",prog->labels.entries[j].name,prog->labels.entries[j].addr);
    }
    printf("+============================================+\n\n");
}
