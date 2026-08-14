#include "cockpit/modules/voice/assistant/deterministic_command_router.h"

#include <iostream>
#include <string>

#include "cockpit/modules/voice/assistant/transcript_normalizer.h"

namespace {

bool ExpectNormalization(const std::string& raw, const std::string& expected) {
  const std::string actual = cockpit::voice::TranscriptNormalizer::Normalize(raw);
  if (actual == expected) {
    return true;
  }
  std::cerr << "normalization mismatch raw=" << raw << " expected=" << expected
            << " actual=" << actual << '\n';
  return false;
}

bool ExpectRoute(const std::string& raw, cockpit::voice::VoiceIntent expected_intent,
                 cockpit::voice::VoiceAction expected_action) {
  const std::string normalized = cockpit::voice::TranscriptNormalizer::Normalize(raw);
  const auto route = cockpit::voice::DeterministicCommandRouter().Route(normalized);
  if (route.intent == expected_intent && route.action == expected_action) {
    return true;
  }
  std::cerr << "route mismatch raw=" << raw << " normalized=" << normalized
            << " expected_intent=" << cockpit::voice::ToString(expected_intent)
            << " actual_intent=" << cockpit::voice::ToString(route.intent)
            << " expected_action=" << cockpit::voice::ToString(expected_action)
            << " actual_action=" << cockpit::voice::ToString(route.action) << '\n';
  return false;
}

}  // namespace

int main() {
  using cockpit::voice::VoiceAction;
  using cockpit::voice::VoiceIntent;
  if (!ExpectNormalization("  OPEN CAMERA  ", "open camera") ||
      !ExpectNormalization("OPEN   CAMERA", "open camera") ||
      !ExpectNormalization("ＯＰＥＮ　ＣＡＭＥＲＡ", "open camera") ||
      !ExpectNormalization("don’t open camera", "don't open camera") ||
      !ExpectNormalization("don‘t open camera", "don't open camera") ||
      !ExpectNormalization("打开相机！", "打开相机") ||
      !ExpectNormalization("查看车辆状态，电量多少？", "查看车辆状态 电量多少")) {
    return 1;
  }

  if (!ExpectRoute("open camera", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("please open camera", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("open the camera", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("please open the camera", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("ＯＰＥＮ　ＣＡＭＥＲＡ！", VoiceIntent::kOpenCamera,
                   VoiceAction::kOpenCamera) ||
      !ExpectRoute("打开相机", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("请打开相机", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("帮我打开相机", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("打开相机！", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("打开摄像头", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("请打开摄像头", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("帮我打开摄像头", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera) ||
      !ExpectRoute("play music", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic) ||
      !ExpectRoute("please play music", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic) ||
      !ExpectRoute("播放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic) ||
      !ExpectRoute("请播放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic) ||
      !ExpectRoute("帮我播放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic) ||
      !ExpectRoute("放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic) ||
      !ExpectRoute("请放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic) ||
      !ExpectRoute("vehicle status", VoiceIntent::kShowVehicleStatus,
                   VoiceAction::kQueryVehicleStatus) ||
      !ExpectRoute("battery level", VoiceIntent::kShowVehicleStatus,
                   VoiceAction::kQueryVehicleStatus) ||
      !ExpectRoute("show vehicle status", VoiceIntent::kShowVehicleStatus,
                   VoiceAction::kQueryVehicleStatus) ||
      !ExpectRoute("show battery level", VoiceIntent::kShowVehicleStatus,
                   VoiceAction::kQueryVehicleStatus) ||
      !ExpectRoute("车辆状态", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus) ||
      !ExpectRoute("查看车辆状态", VoiceIntent::kShowVehicleStatus,
                   VoiceAction::kQueryVehicleStatus) ||
      !ExpectRoute("请查看车辆状态", VoiceIntent::kShowVehicleStatus,
                   VoiceAction::kQueryVehicleStatus) ||
      !ExpectRoute("查看电量", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus) ||
      !ExpectRoute("请查看电量", VoiceIntent::kShowVehicleStatus,
                   VoiceAction::kQueryVehicleStatus) ||
      !ExpectRoute("电量多少", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus)) {
    return 1;
  }

  const std::string rejected[] = {
      "不要打开相机",
      "别打开相机",
      "不打开相机",
      "请勿打开相机",
      "不用打开相机",
      "不需要打开相机",
      "不打开摄像头",
      "不要打开摄像头",
      "别打开摄像头",
      "不播放音乐",
      "请勿播放音乐",
      "不用播放音乐",
      "不需要播放音乐",
      "不要播放音乐",
      "别放音乐",
      "don't open camera",
      "don’t open camera",
      "don‘t open camera",
      "do not open camera",
      "do not ever open camera",
      "never open camera",
      "can't open camera",
      "cannot open camera",
      "not open camera",
      "don't play music",
      "do not play music",
      "never play music",
      "can't play music",
      "not play music",
      "不要现在打开相机",
      "不要打开相机，播放音乐",
      "不能打开相机",
      "禁止打开相机",
      "无需打开相机",
      "不必打开相机",
      "不能播放音乐",
      "禁止播放音乐",
      "打开相机并播放音乐",
      "这个画面不错，打开相机",
      "以后打开相机",
      "我想打开相机",
      "open camera and play music",
      "open camera later",
      "if possible open camera",
      "i want to open camera",
      "set speed to 100",
      "把速度调到100",
      "速度设置为80",
      "turn steering 20 degrees",
      "油门50%",
      "reopen camera settings",
      "play musical instruments",
      "cameraopen camera view",
      "camera open camera view",
      "display music playlist",
  };
  for (const std::string& text : rejected) {
    if (!ExpectRoute(text, VoiceIntent::kUnknown, VoiceAction::kNone)) {
      return 1;
    }
  }

  std::cout << "deterministic command router tests passed\n";
  return 0;
}
