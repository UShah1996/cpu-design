/**
 * ============================================================
 * CMPE-220 Software CPU — Assembler
 * ============================================================
 * Two-pass assembler:
 *   Pass 1: tokenize, collect labels and their addresses
 *   Pass 2: encode each instruction into 16-bit words
 *
 * Syntax:
 *   LABEL:          — defines a label at current address
 *   ADD  R0, R1     — register mode
 *   LOAD R0, #5     — immediate mode  (# prefix)
 *   LOAD R0, [10]   — direct memory   (brackets + number)
 *   LOAD R0, [R1]   — register indirect (brackets + register)
 *   ; comment       — rest of line ignored
 *   .data 0x1234    — emit raw word into output
 *   .org 0x0100     — set current address
 * ============================================================
 */

#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include "../isa/isa.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

class Assembler {
public:
    struct AssembledProgram {
        std::vector<Word>   words;         // encoded instruction words
        Address             load_address;  // where to load in memory
        std::map<std::string,Address> labels; // label → address map
    };

    // ── Assemble source text → program ────────────────────────
    AssembledProgram assemble(const std::string& source,
                              Address load_addr = CODE_START) {
        this->origin = load_addr;
        labels.clear();
        output.clear();

        auto lines = split_lines(source);

        // PASS 1: collect labels
        Address addr = load_addr;
        for (auto& line : lines) {
            auto tokens = tokenize(line);
            if (tokens.empty()) continue;
            if (is_directive(tokens[0])) {
                handle_directive_pass1(tokens, addr);
                continue;
            }
            if (is_label(tokens[0])) {
                labels[tokens[0].substr(0, tokens[0].size()-1)] = addr;
                tokens.erase(tokens.begin());
                if (tokens.empty()) continue;
            }
            if (!tokens.empty() && !is_directive(tokens[0]))
                addr++;  // every instruction = 1 word
        }

        // PASS 2: encode instructions
        addr = load_addr;
        for (auto& line : lines) {
            auto tokens = tokenize(line);
            if (tokens.empty()) continue;
            if (is_directive(tokens[0])) {
                handle_directive_pass2(tokens, addr);
                continue;
            }
            if (is_label(tokens[0])) {
                tokens.erase(tokens.begin());
                if (tokens.empty()) continue;
            }
            if (!tokens.empty()) {
                Word w = encode_instruction(tokens, addr);
                output.push_back(w);
                addr++;
            }
        }

        return { output, load_addr, labels };
    }

    // ── Print assembled listing ────────────────────────────────
    void print_listing(const AssembledProgram& prog) const {
        printf("\n╔════════════════════════════════════════════╗\n");
        printf("║           ASSEMBLED LISTING                ║\n");
        printf("╠═══════════╦═══════╦═══════════════════════╣\n");
        printf("║  ADDRESS  ║  HEX  ║  DISASSEMBLY          ║\n");
        printf("╠═══════════╬═══════╬═══════════════════════╣\n");

        Address a = prog.load_address;
        for (Word w : prog.words) {
            Opcode op   = decode_opcode(w);
            int    dest = decode_dest(w);
            Mode   mode = decode_mode(w);
            int    src  = decode_src(w);

            char dis[48];
            switch(mode) {
                case Mode::REG:
                    snprintf(dis,sizeof(dis),"%-5s R%d, R%d",
                        opcode_name(op),dest,src&0xF); break;
                case Mode::IMM:
                    snprintf(dis,sizeof(dis),"%-5s R%d, #%d",
                        opcode_name(op),dest,src); break;
                case Mode::DIRECT:
                    snprintf(dis,sizeof(dis),"%-5s R%d, [%02X]",
                        opcode_name(op),dest,src); break;
                case Mode::INDIRECT:
                    snprintf(dis,sizeof(dis),"%-5s R%d, [R%d]",
                        opcode_name(op),dest,src&0xF); break;
            }
            printf("║  %04X     ║ %04X ║ %-22s║\n", a, w, dis);
            a++;
        }

        printf("╠═══════════╩═══════╩═══════════════════════╣\n");
        printf("║  Labels:                                   ║\n");
        for (auto& [name,addr2] : prog.labels) {
            printf("║    %-20s → %04X          ║\n", name.c_str(), addr2);
        }
        printf("╚════════════════════════════════════════════╝\n\n");
    }

private:
    Address              origin = CODE_START;
    std::map<std::string,Address> labels;
    std::vector<Word>    output;

    // ── Tokenize one line ─────────────────────────────────────
    std::vector<std::string> tokenize(const std::string& line) {
        std::vector<std::string> tokens;
        std::string s = line;

        // Strip comments
        auto sc = s.find(';');
        if (sc != std::string::npos) s = s.substr(0, sc);

        // Trim and split on whitespace and commas
        std::string tok;
        for (char c : s) {
            if (c == ',' || c == ' ' || c == '\t') {
                if (!tok.empty()) { tokens.push_back(tok); tok.clear(); }
            } else {
                tok += toupper(c);
            }
        }
        if (!tok.empty()) tokens.push_back(tok);
        return tokens;
    }

    std::vector<std::string> split_lines(const std::string& src) {
        std::vector<std::string> lines;
        std::istringstream ss(src);
        std::string line;
        while(std::getline(ss, line)) lines.push_back(line);
        return lines;
    }

    bool is_label(const std::string& t) {
        return !t.empty() && t.back() == ':';
    }
    bool is_directive(const std::string& t) {
        return !t.empty() && t[0] == '.';
    }

    // ── Resolve a token to a numeric value ───────────────────
    // Handles: numbers, hex (0x...), register names, labels
    int resolve(const std::string& tok, Address current_addr) {
        if (tok.empty()) return 0;

        // Hex literal: 0xABCD
        if (tok.size() > 2 && tok[0]=='0' && tok[1]=='X')
            return static_cast<int>(std::stoul(tok.substr(2), nullptr, 16));

        // Decimal literal
        if (std::isdigit(tok[0]) || (tok[0]=='-' && tok.size()>1))
            return std::stoi(tok);

        // Label reference
        if (labels.count(tok)) return labels.at(tok);

        // Register: R0-R15
        if (tok[0]=='R' && tok.size()>1 && std::isdigit(tok[1]))
            return std::stoi(tok.substr(1));

        // Named registers
        if (tok=="SP") return SP;
        if (tok=="LR") return LR;
        if (tok=="PC") return PC;

        // Immediate with # prefix (strip it)
        if (tok[0]=='#') return resolve(tok.substr(1), current_addr);

        throw std::runtime_error("Unknown token: " + tok);
    }

    // ── Determine addressing mode from operand token ──────────
    Mode parse_mode(const std::string& tok) {
        if (tok.empty()) return Mode::REG;

        // [Rn] = register indirect
        if (tok.front()=='[' && tok.back()==']') {
            std::string inner = tok.substr(1, tok.size()-2);
            if (inner[0]=='R' || inner=="SP" || inner=="LR" || inner=="PC")
                return Mode::INDIRECT;
            return Mode::DIRECT;
        }

        // #n = immediate
        if (tok[0]=='#') return Mode::IMM;

        // Rn = register
        if (tok[0]=='R' || tok=="SP" || tok=="LR" || tok=="PC")
            return Mode::REG;

        // Label or plain number = immediate
        if (labels.count(tok) || std::isdigit(tok[0]))
            return Mode::IMM;

        return Mode::IMM;
    }

    // ── Strip brackets from [token] ───────────────────────────
    std::string strip_brackets(const std::string& tok) {
        if (tok.front()=='[' && tok.back()==']')
            return tok.substr(1, tok.size()-2);
        if (tok[0]=='#') return tok.substr(1);
        return tok;
    }

    // ── Encode one instruction ────────────────────────────────
    Word encode_instruction(const std::vector<std::string>& tokens,
                            Address current_addr) {
        if (tokens.empty()) return 0;

        std::string mnemonic = tokens[0];

        // Map mnemonic → opcode
        static const std::map<std::string,Opcode> opmap = {
            {"ADD",Opcode::ADD},{"SUB",Opcode::SUB},{"AND",Opcode::AND},
            {"OR",Opcode::OR},  {"NOT",Opcode::NOT},{"SHL",Opcode::SHL},
            {"SHR",Opcode::SHR},{"SAR",Opcode::SAR},{"LOAD",Opcode::LOAD},
            {"STORE",Opcode::STORE},{"JMP",Opcode::JMP},{"BEQ",Opcode::BEQ},
            {"BNE",Opcode::BNE},{"BLT",Opcode::BLT},{"CALL",Opcode::CALL},
            {"RET",Opcode::RET},
        };

        if (!opmap.count(mnemonic))
            throw std::runtime_error("Unknown mnemonic: " + mnemonic);

        Opcode op   = opmap.at(mnemonic);
        int    dest = 0;
        Mode   mode = Mode::IMM;
        int    src  = 0;

        // RET takes no operands
        if (op == Opcode::RET) {
            return encode(op, 0, Mode::REG, 0);
        }

        // NOT takes one register operand
        if (op == Opcode::NOT && tokens.size() >= 2) {
            dest = resolve(tokens[1], current_addr) & 0xF;
            return encode(op, dest, Mode::REG, 0);
        }

        // All other instructions: dest, src
        if (tokens.size() >= 2) {
            dest = resolve(tokens[1], current_addr) & 0xF;
        }
        if (tokens.size() >= 3) {
            std::string src_tok = tokens[2];
            mode = parse_mode(src_tok);
            std::string inner = strip_brackets(src_tok);
            src = resolve(inner, current_addr) & 0x3F;
        }

        return encode(op, dest, mode, src);
    }

    // ── Directive handlers ────────────────────────────────────
    void handle_directive_pass1(const std::vector<std::string>& tokens,
                                Address& addr) {
        if (tokens[0]==".ORG" && tokens.size()>1) {
            addr = static_cast<Address>(
                std::stoul(tokens[1].substr(
                    tokens[1][0]=='0' && tokens[1].size()>2 ? 2 : 0),
                    nullptr, 16));
        }
        // .DATA counts as one word
        if (tokens[0]==".DATA") addr++;
    }

    void handle_directive_pass2(const std::vector<std::string>& tokens,
                                Address& addr) {
        if (tokens[0]==".DATA" && tokens.size()>1) {
            std::string val = tokens[1];
            Word w;
            if (val.size()>2 && val[0]=='0' && val[1]=='X')
                w = static_cast<Word>(std::stoul(val.substr(2),nullptr,16));
            else
                w = static_cast<Word>(std::stoi(val));
            output.push_back(w);
            addr++;
        }
        if (tokens[0]==".ORG" && tokens.size()>1) {
            addr = static_cast<Address>(
                std::stoul(tokens[1].substr(
                    tokens[1][0]=='0' && tokens[1].size()>2 ? 2 : 0),
                    nullptr, 16));
        }
    }
};

#endif // ASSEMBLER_H
