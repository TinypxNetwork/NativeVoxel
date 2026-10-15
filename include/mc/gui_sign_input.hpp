#pragma once

#include "mc/gui_text_input.hpp"
#include <array>

namespace mc::gui {

// GuiEditSign appends text to one of four rows, measured in unscaled font pixels.
// A narrow glyph can fit more often than a wide glyph; there is no 15-char limit.
class SignInput {
public:
    using Measure = std::function<float(const std::string&)>;
    void load(const std::array<std::string,4>& lines) {
        row_=0;
        for(std::size_t i=0;i<lines.size();++i)fields_[i].setText(lines[i]);
    }
    int row() const { return row_; }
    void moveLine(int direction) { row_=(row_+direction%4+4)%4; }
    const std::string& line(int row) const { return fields_[static_cast<std::size_t>(row)].text(); }
    std::array<std::string,4> lines() const { return {{line(0),line(1),line(2),line(3)}}; }
    void backspace() { fields_[static_cast<std::size_t>(row_)].erase(true); }
    void insert(const std::u32string& text,const Measure& measure) {
        auto& field=fields_[static_cast<std::size_t>(row_)];
        for(const auto cp:text) {
            auto candidate=field;
            candidate.insert(std::u32string(1,cp));
            if(measure(candidate.text())<=90.f)field=std::move(candidate);
        }
    }
private:
    std::array<TextInput,4> fields_{{TextInput(384),TextInput(384),TextInput(384),TextInput(384)}};
    int row_{};
};

} // namespace mc::gui
