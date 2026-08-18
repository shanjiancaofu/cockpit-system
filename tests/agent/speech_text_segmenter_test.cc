#include "agent/speech/tts/speech_text_segmenter.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool CheckSegments(const std::vector<std::string>& actual, const std::vector<std::string>& expected,
                   const char* message) {
  if (actual == expected) {
    return true;
  }
  std::cerr << message << '\n';
  for (const auto& segment : actual) {
    std::cerr << "  actual: [" << segment << "]\n";
  }
  return false;
}

}  // namespace

int main() {
  using cockpit::voice::SplitSpeechText;
  if (!CheckSegments(SplitSpeechText("Camera opened. Music stopped? Ready!"),
                     {"Camera opened.", "Music stopped?", "Ready!"},
                     "English sentence boundaries were not preserved") ||
      !CheckSegments(SplitSpeechText("相机已打开。音乐已停止！请继续？"),
                     {"相机已打开。", "音乐已停止！", "请继续？"},
                     "Chinese sentence boundaries were not preserved") ||
      !CheckSegments(SplitSpeechText("first line\n\nsecond line"), {"first line", "second line"},
                     "line boundaries or empty segments were handled incorrectly") ||
      !CheckSegments(SplitSpeechText("one two three four", 9U), {"one two", "three", "four"},
                     "long English text did not prefer whitespace boundaries") ||
      !CheckSegments(SplitSpeechText("一二三四五六七八九", 4U), {"一二三四", "五六七八", "九"},
                     "long Chinese text was not split on UTF-8 boundaries") ||
      !CheckSegments(SplitSpeechText("  hello。  "), {"hello。"},
                     "outer whitespace was not trimmed") ||
      !CheckSegments(SplitSpeechText("", 10U), {}, "empty text produced a segment") ||
      !CheckSegments(SplitSpeechText("text", 0U), {}, "zero length limit produced a segment")) {
    return 1;
  }
  std::cout << "speech text segmenter tests passed\n";
  return 0;
}
