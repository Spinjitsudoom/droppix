#include <gtest/gtest.h>
#include <vector>
#include "audio_sink.h"
using namespace droppix;

TEST(DroppixAudioSink, AdoptsExistingSinkWithoutCreating) {
  std::vector<std::string> calls;
  DroppixAudioSink::Runner fake = [&](const std::string& cmd) {
    calls.push_back(cmd);
    if (cmd.find("list short sinks") != std::string::npos)
      return std::make_pair(true, std::string("12\tdroppix-audio\tPipeWire\ts16le 2ch 48000Hz\tIDLE\n"));
    return std::make_pair(true, std::string());
  };
  DroppixAudioSink sink(fake);
  sink.ensure();
  EXPECT_FALSE(sink.created_here());
  sink.release();
  for (auto& c : calls) EXPECT_EQ(c.find("load-module"), std::string::npos);
  for (auto& c : calls) EXPECT_EQ(c.find("unload-module"), std::string::npos);
}

TEST(DroppixAudioSink, CreatesThenUnloadsOnlyWhatItCreated) {
  std::vector<std::string> calls;
  DroppixAudioSink::Runner fake = [&](const std::string& cmd) {
    calls.push_back(cmd);
    if (cmd.find("list short sinks") != std::string::npos)
      return std::make_pair(true, std::string());            // no existing sink
    if (cmd.find("load-module") != std::string::npos)
      return std::make_pair(true, std::string("42\n"));      // pactl prints module index
    return std::make_pair(true, std::string());
  };
  DroppixAudioSink sink(fake);
  sink.ensure();
  EXPECT_TRUE(sink.created_here());
  sink.release();
  bool unloaded42 = false;
  for (auto& c : calls) if (c.find("unload-module 42") != std::string::npos) unloaded42 = true;
  EXPECT_TRUE(unloaded42);
}
