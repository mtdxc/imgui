#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
extern "C" {
#include <libavformat/avformat.h>
}

struct PacketEdit {
    int64_t packet_index = -1;
    int stream_index = -1;
    int64_t original_pts = AV_NOPTS_VALUE;
    int64_t original_dts = AV_NOPTS_VALUE;
    int64_t edited_pts = AV_NOPTS_VALUE;
    int64_t edited_dts = AV_NOPTS_VALUE;
    int64_t duration = 0;
    bool deleted = false;
};

class FrameEditor {
public:
    bool Load(const std::string& input_path, std::string& err);
    bool SaveAs(const std::string& output_path, std::string& err);

    size_t StreamCount() const { return stream_index_map_.size(); }
    std::vector<int>* Stream(int idx) { 
        auto it = stream_index_map_.find(idx);
        if (it == stream_index_map_.end()) {
            return nullptr;
        }
        return &it->second; 
    }
    size_t EditCount() const { return edits_.size(); }
    PacketEdit* Edit(int idx) { 
        if (idx < 0 || idx >= static_cast<int>(edits_.size())) {
            return nullptr;
        }
        return &edits_[idx]; 
    }

    const std::string& InputPath() const { return input_path_; }

private:
    void Clear();

    std::string input_path_;
    std::vector<PacketEdit> edits_;
    std::map<int, std::vector<int>> stream_index_map_;
};
