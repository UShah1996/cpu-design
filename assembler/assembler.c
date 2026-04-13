/*
 * ============================================================
 * CMPE-220 Software CPU — Two-Pass Assembler Implementation
 * ============================================================
 */

#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Internal: token list for one line ───────────────────────*/
typedef struct {
    char tokens[MAX_TOKENS][MAX_LINE_LEN];
    int  count;
} TokenLine;

/* ── Internal function declarations ─────────────────────────*/
static void      str_toupper      (char* s);
static void      str_trim         (char* s);
static int       is_label_def     (const char* s);
static int       is_directive     (const char* s);
static TokenLine tokenize_line    (const char* line);
static void      label_add        (LabelTable* lt, const char* name, Address addr);
static Address   label_find       (const LabelTable* lt, const char* name);
static int       resolve_token    (const char* tok, const LabelTable* lt, int* ok);
static AddrMode  parse_mode       (const char* tok, const LabelTable* lt);
static int       mnemonic_to_opcode(const char* m, Opcode* op);
static Word      encode_from_tokens(const TokenLine* tl, const LabelTable* lt,
                                    Address cur_addr, int* err);

/* ─────────────────────────────────────────────────────────────
 * STRING HELPERS
 * ─────────────────────────────────────────────────────────────*/

static void str_toupper(char* s) {
    while (*s) { *s = (char)toupper((unsigned char)*s); s++; }
}

static void str_trim(char* s) {
    int start = 0, end = (int)strlen(s) - 1;
    while (s[start] == ' ' || s[start] == '\t') start++;
    while (end >= start && (s[end] == ' ' || s[end] == '\t' ||
                             s[end] == '\r' || s[end] == '\n')) end--;
    memmove(s, s + start, end - start + 1);
    s[end - start + 1] = '\0';
}

static int is_label_def(const char* s) {
    int len = (int)strlen(s);
    return len > 1 && s[len-1] == ':';
}

static int is_directive(const char* s) {
    return s[0] == '.';
}

/* ── Tokenize one line: split on spaces, commas, tabs ────────*/
static TokenLine tokenize_line(const char* line) {
    TokenLine tl;
    char buf[MAX_LINE_LEN];
    int  ti = 0;
    tl.count = 0;

    strncpy(buf, line, MAX_LINE_LEN - 1);
    buf[MAX_LINE_LEN - 1] = '\0';

    /* Strip comment */
    {
        char* sc = strchr(buf, ';');
        if (sc) *sc = '\0';
    }

    /* Split on whitespace and commas */
    {
        char* p = buf;
        while (*p && ti < MAX_TOKENS) {
            int i = 0;
            while (*p == ' ' || *p == '\t' || *p == ',') p++;
            if (!*p) break;
            while (*p && *p != ' ' && *p != '\t' && *p != ',') {
                tl.tokens[ti][i++] = *p++;
            }
            tl.tokens[ti][i] = '\0';
            if (i > 0) {
                str_toupper(tl.tokens[ti]);
                ti++;
                tl.count = ti;
            }
        }
    }
    return tl;
}

/* ─────────────────────────────────────────────────────────────
 * LABEL TABLE
 * ─────────────────────────────────────────────────────────────*/

static void label_add(LabelTable* lt, const char* name, Address addr) {
    size_t len;
    if (lt->count >= MAX_LABELS) return;
    len = strlen(name);
    if (len > MAX_LABEL_LEN - 1) len = MAX_LABEL_LEN - 1;
    memcpy(lt->entries[lt->count].name, name, len);
    lt->entries[lt->count].name[len] = '\0';
    lt->entries[lt->count].addr = addr;
    lt->count++;
}

/* Returns address of label, or 0xFFFF if not found */
static Address label_find(const LabelTable* lt, const char* name) {
    int i;
    for (i = 0; i < lt->count; i++) {
        if (strcmp(lt->entries[i].name, name) == 0)
            return lt->entries[i].addr;
    }
    return 0xFFFF;
}

/* ─────────────────────────────────────────────────────────────
 * TOKEN RESOLUTION
 * ─────────────────────────────────────────────────────────────*/

/* Resolve a token to a numeric value.
 * Handles: 0xABCD, 123, R0-R15, SP/LR/PC, #n, LABEL
 */
static int resolve_token(const char* tok, const LabelTable* lt, int* ok) {
    *ok = 1;

    if (!tok || !tok[0]) { *ok = 0; return 0; }

    if (tok[0] == '#') return resolve_token(tok + 1, lt, ok);

    if (tok[0] == '[') {
        int len = (int)strlen(tok);
        if (tok[len-1] == ']') {
            char inner[MAX_LINE_LEN];
            strncpy(inner, tok + 1, len - 2);
            inner[len - 2] = '\0';
            return resolve_token(inner, lt, ok);
        }
    }

    if (tok[0]=='0' && (tok[1]=='X' || tok[1]=='x'))
        return (int)strtoul(tok + 2, NULL, 16);

    if (isdigit((unsigned char)tok[0]) ||
        (tok[0]=='-' && isdigit((unsigned char)tok[1])))
        return atoi(tok);

    if (tok[0]=='R' && isdigit((unsigned char)tok[1]))
        return atoi(tok + 1);

    if (strcmp(tok, "SP") == 0) return REG_SP;
    if (strcmp(tok, "LR") == 0) return REG_LR;
    if (strcmp(tok, "PC") == 0) return REG_PC;

    {
        Address a = label_find(lt, tok);
        if (a != 0xFFFF) return (int)a;
    }

    *ok = 0;
    return 0;
}

/* Determine addressing mode from a token */
static AddrMode parse_mode(const char* tok, const LabelTable* lt) {
    if (!tok || !tok[0]) return MODE_REG;

    if (tok[0] == '[') {
        int len = (int)strlen(tok);
        if (len > 2 && tok[len-1] == ']') {
            char inner[MAX_LINE_LEN];
            strncpy(inner, tok + 1, len - 2);
            inner[len - 2] = '\0';
            if ((inner[0]=='R' && isdigit((unsigned char)inner[1])) ||
                strcmp(inner,"SP")==0 || strcmp(inner,"LR")==0 ||
                strcmp(inner,"PC")==0) {
                return MODE_INDIRECT;
            }
            return MODE_DIRECT;
        }
    }

    if (tok[0] == '#') return MODE_IMM;

    if ((tok[0]=='R' && isdigit((unsigned char)tok[1])) ||
        strcmp(tok,"SP")==0 || strcmp(tok,"LR")==0 ||
        strcmp(tok,"PC")==0) {
        return MODE_REG;
    }

    (void)lt;
    return MODE_IMM;
}

/* Map mnemonic string to Opcode */
static int mnemonic_to_opcode(const char* m, Opcode* op) {
    if (!strcmp(m,"ADD"))   { *op = OP_ADD;   return 1; }
    if (!strcmp(m,"SUB"))   { *op = OP_SUB;   return 1; }
    if (!strcmp(m,"AND"))   { *op = OP_AND;   return 1; }
    if (!strcmp(m,"OR"))    { *op = OP_OR;    return 1; }
    if (!strcmp(m,"NOT"))   { *op = OP_NOT;   return 1; }
    if (!strcmp(m,"SHL"))   { *op = OP_SHL;   return 1; }
    if (!strcmp(m,"SHR"))   { *op = OP_SHR;   return 1; }
    if (!strcmp(m,"SAR"))   { *op = OP_SAR;   return 1; }
    if (!strcmp(m,"LOAD"))  { *op = OP_LOAD;  return 1; }
    if (!strcmp(m,"STORE")) { *op = OP_STORE; return 1; }
    if (!strcmp(m,"JMP"))   { *op = OP_JMP;   return 1; }
    if (!strcmp(m,"BEQ"))   { *op = OP_BEQ;   return 1; }
    if (!strcmp(m,"BNE"))   { *op = OP_BNE;   return 1; }
    if (!strcmp(m,"BLT"))   { *op = OP_BLT;   return 1; }
    if (!strcmp(m,"CALL"))  { *op = OP_CALL;  return 1; }
    if (!strcmp(m,"RET"))   { *op = OP_RET;   return 1; }
    return 0;
}

/* Encode one instruction from tokens */
static Word encode_from_tokens(const TokenLine* tl, const LabelTable* lt,
                                Address cur_addr, int* err) {
    Opcode   op;
    int      dest = 0, src = 0, ok = 1;
    AddrMode mode = MODE_IMM;
    *err = 0;

    if (tl->count < 1) return 0;

    if (!mnemonic_to_opcode(tl->tokens[0], &op)) { *err = 1; return 0; }

    if (op == OP_RET) return encode_instr(op, 0, MODE_REG, 0);

    if (op == OP_NOT && tl->count >= 2) {
        dest = resolve_token(tl->tokens[1], lt, &ok) & 0xF;
        return encode_instr(op, dest, MODE_REG, 0);
    }

    if (op == OP_JMP || op == OP_BEQ || op == OP_BNE ||
        op == OP_BLT || op == OP_CALL) {
        dest = 0;
        if (tl->count >= 2) {
            mode = parse_mode(tl->tokens[1], lt);
            src  = resolve_token(tl->tokens[1], lt, &ok) & 0x3F;
            if (!ok) { *err = 1; return 0; }
        }
        return encode_instr(op, dest, mode, src);
    }

    if (tl->count >= 2) {
        dest = resolve_token(tl->tokens[1], lt, &ok) & 0xF;
        if (!ok) { *err = 1; return 0; }
    }
    if (tl->count >= 3) {
        mode = parse_mode(tl->tokens[2], lt);
        src  = resolve_token(tl->tokens[2], lt, &ok) & 0x3F;
        if (!ok) { *err = 1; return 0; }
    }

    (void)cur_addr;
    return encode_instr(op, dest, mode, src);
}

/* ─────────────────────────────────────────────────────────────
 * MAIN ASSEMBLE FUNCTION
 * Two-pass assembly: Pass1=collect labels, Pass2=encode
 * ─────────────────────────────────────────────────────────────*/
AsmProgram asm_assemble(const char* source, Address load_addr) {
    AsmProgram prog;
    char  lines[MAX_SOURCE_LINES][MAX_LINE_LEN];
    int   num_lines = 0;
    int   i;

    memset(&prog, 0, sizeof(prog));
    prog.load_addr = load_addr;
    prog.ok        = 1;

    /* Split source into lines */
    {
        const char* p = source;
        int li = 0, ci = 0;
        while (*p && num_lines < MAX_SOURCE_LINES) {
            if (*p == '\n') {
                lines[li][ci] = '\0';
                str_trim(lines[li]);
                li++; ci = 0;
            } else if (ci < MAX_LINE_LEN - 1) {
                lines[li][ci++] = *p;
            }
            p++;
        }
        if (ci > 0) { lines[li][ci] = '\0'; str_trim(lines[li]); li++; }
        num_lines = li;
    }

    /* ── PASS 1: Scan for labels, count instruction addresses ─*/
    {
        Address addr = load_addr;
        for (i = 0; i < num_lines; i++) {
            TokenLine tl;
            int ti = 0;
            if (!lines[i][0]) continue;

            tl = tokenize_line(lines[i]);
            if (tl.count == 0) continue;

            if (is_label_def(tl.tokens[ti])) {
                char lname[MAX_LABEL_LEN];
                int  llen = (int)strlen(tl.tokens[ti]) - 1;  /* strip ':' */
                if (llen > MAX_LABEL_LEN - 1) llen = MAX_LABEL_LEN - 1;
                memcpy(lname, tl.tokens[ti], llen);
                lname[llen] = '\0';
                label_add(&prog.labels, lname, addr);
                ti++;
                if (ti >= tl.count) continue;
            }

            if (is_directive(tl.tokens[ti])) {
                if (strcmp(tl.tokens[ti], ".ORG") == 0 && tl.count > ti+1) {
                    int ok2;
                    addr = (Address)resolve_token(tl.tokens[ti+1], &prog.labels, &ok2);
                }
                if (strcmp(tl.tokens[ti], ".DATA") == 0) addr++;
                continue;
            }

            if (tl.count > ti) addr++;
        }
    }

    /* ── PASS 2: Encode instructions ─────────────────────────*/
    {
        Address addr = load_addr;
        for (i = 0; i < num_lines; i++) {
            TokenLine tl;
            int ti = 0;
            if (!lines[i][0]) continue;

            tl = tokenize_line(lines[i]);
            if (tl.count == 0) continue;

            if (is_label_def(tl.tokens[ti])) {
                ti++;
                if (ti >= tl.count) continue;
            }

            if (is_directive(tl.tokens[ti])) {
                if (strcmp(tl.tokens[ti], ".ORG") == 0 && tl.count > ti+1) {
                    int ok2;
                    addr = (Address)resolve_token(tl.tokens[ti+1], &prog.labels, &ok2);
                }
                if (strcmp(tl.tokens[ti], ".DATA") == 0 && tl.count > ti+1) {
                    int ok2;
                    Word w = (Word)resolve_token(tl.tokens[ti+1], &prog.labels, &ok2);
                    if (prog.count < MAX_PROGRAM)
                        prog.words[prog.count++] = w;
                    addr++;
                }
                continue;
            }

            if (tl.count > ti) {
                TokenLine shifted = tl;
                if (ti > 0) {
                    int j;
                    for (j = 0; j < tl.count - ti; j++)
                        memcpy(shifted.tokens[j], tl.tokens[j+ti], MAX_LINE_LEN);
                    shifted.count = tl.count - ti;
                }
                {
                    int  err = 0;
                    Word w   = encode_from_tokens(&shifted, &prog.labels, addr, &err);
                    if (err) {
                        prog.ok = 0;
                        /* clamp token to avoid format-truncation warning */
                        {
                            char tok_clamped[64];
                            strncpy(tok_clamped, shifted.tokens[0], sizeof(tok_clamped) - 1);
                            tok_clamped[sizeof(tok_clamped) - 1] = '\0';
                            snprintf(prog.error, sizeof(prog.error),
                                "Line %d: unknown mnemonic '%s'", i+1, tok_clamped);
                        }
                        return prog;
                    }
                    if (prog.count < MAX_PROGRAM)
                        prog.words[prog.count++] = w;
                    addr++;
                }
            }
        }
    }

    return prog;
}

/* ── Print assembly listing ──────────────────────────────────*/
void asm_print_listing(const AsmProgram* prog) {
    int i;
    Address a = prog->load_addr;

    printf("\n+============================================+\n");
    printf("|         ASSEMBLED LISTING                  |\n");
    printf("+----------+-------+------------------------+\n");
    printf("|  ADDRESS |  HEX  |  DISASSEMBLY           |\n");
    printf("+----------+-------+------------------------+\n");

    for (i = 0; i < prog->count; i++) {
        Word     w    = prog->words[i];
        Opcode   op   = decode_opcode(w);
        int      dest = decode_dest(w);
        AddrMode mode = decode_mode(w);
        int      src  = decode_src(w);
        char     dis[48];

        switch(mode) {
            case MODE_REG:
                snprintf(dis,sizeof(dis),"%-5s R%d, R%d",
                    opcode_name(op), dest, src & 0xF); break;
            case MODE_IMM:
                snprintf(dis,sizeof(dis),"%-5s R%d, #%d",
                    opcode_name(op), dest, src); break;
            case MODE_DIRECT:
                snprintf(dis,sizeof(dis),"%-5s R%d, [%02X]",
                    opcode_name(op), dest, src); break;
            case MODE_INDIRECT:
                snprintf(dis,sizeof(dis),"%-5s R%d, [R%d]",
                    opcode_name(op), dest, src & 0xF); break;
            default:
                snprintf(dis,sizeof(dis),"???"); break;
        }
        printf("|  %04X    | %04X | %-23s|\n", a, w, dis);
        a++;
    }

    printf("+----------+-------+------------------------+\n");
    if (prog->labels.count > 0) {
        printf("|  Labels:                                   |\n");
        for (i = 0; i < prog->labels.count; i++) {
            printf("|    %-20s -> %04X         |\n",
                prog->labels.entries[i].name,
                prog->labels.entries[i].addr);
        }
    }
    printf("+============================================+\n\n");
}

/* ── Load assembled program into memory ──────────────────────*/
void asm_load_into_memory(const AsmProgram* prog, Memory* mem) {
    int i;
    for (i = 0; i < prog->count; i++) {
        mem->ram[prog->load_addr + i] = prog->words[i];
    }
}

/* ── Extended: pool-aware branch encoder ─────────────────────
 * For branch/jump instructions targeting labels with addr > 63,
 * automatically places the address in the constant pool (0x00-0x0F)
 * and uses DIRECT mode so the 6-bit src field addresses the pool slot.
 */
AsmProgram asm_assemble_with_pool(const char* source,
                                   Address load_addr,
                                   Word* pool,
                                   int pool_base_used) {
    AsmProgram prog = asm_assemble(source, load_addr);
    int i, pool_next;

    if (!prog.ok) return prog;

    pool_next = pool_base_used;

    for (i = 0; i < prog.count; i++) {
        Word     w    = prog.words[i];
        Opcode   op   = decode_opcode(w);
        AddrMode mode;
        int      src;

        if (op != OP_JMP && op != OP_BEQ && op != OP_BNE &&
            op != OP_BLT && op != OP_CALL) continue;

        mode = decode_mode(w);
        src  = decode_src(w);

        if (mode == MODE_IMM && src > 0x3F) {
            int slot = -1, j;
            for (j = 0; j < pool_next; j++) {
                if (pool[j] == (Word)src) { slot = j; break; }
            }
            if (slot < 0 && pool_next < 16) {
                slot = pool_next++;
                pool[slot] = (Word)src;
            }
            if (slot >= 0) {
                prog.words[i] = encode_instr(op, decode_dest(w),
                                              MODE_DIRECT, slot & 0x3F);
            }
        }
    }
    return prog;
}
