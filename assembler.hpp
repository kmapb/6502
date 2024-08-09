#pragma once

#include "6502.hpp"

#include <string>
#include <vector>
#include <map>

// Returns bytes consumed
typedef uint16_t MemLoc;
extern size_t assemble_instr(Mnemonic mnem,
                             AddressingMode addr,
                             MemLoc dir16,
                             uint8_t* oldPC);

struct Assembler {
    class Label {
        std::string name;
        MemLoc location;    
        enum State {
            UNINIT, RESOLVED
        } state = UNINIT;
      public:	
		Label(const std::string& nm, MemLoc loc, State st = UNINIT)
        : name(nm)
        , location(loc)
        , state(st) { }

        Label()
        : name("")
        , location(0),
        state(UNINIT) {}

        std::vector<MemLoc> word_references;
        std::vector<MemLoc> byte_references;

        void resolve(MemLoc location);
    };

    Assembler(Memory& mem) : 
    mem_(mem), org_(0) { }

    Assembler&
    org(MemLoc location) {
        org_ = location;
        return *this;
    }

    Assembler&
    label(const std::string& name) {
        labels_[name] = Label(name, org_);
        auto& lbl = labels_.find(name)->second;
        lbl.word_references.emplace_back(org_);
        return *this;
    }

    void reference(Label& lbl) {
        lbl.word_references.push_back(org_);
    }

    Assembler&
    operator()(Mnemonic mnem, AddressingMode addr, uint16_t imm = 0) {
        org_ += assemble_instr(mnem, addr, imm, &mem_[org_]);
        return *this;
    }

    Assembler&
    operator()(Mnemonic mnem, Label label);

protected:
    Memory& mem_;
    MemLoc org_;
    std::map<std::string, Label> labels_;
};
