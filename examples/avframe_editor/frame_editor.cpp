#include "frame_editor.hpp"

#include <unordered_map>

extern "C" {
#include <libavutil/error.h>
}

namespace {

std::string AvErr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buf, sizeof(buf));
    return std::string(buf);
}

} // namespace

void FrameEditor::Clear() {
    input_path_.clear();
    edits_.clear();
    stream_index_map_.clear();
}

#define TIME_BASE 1000
bool FrameEditor::Load(const std::string& input_path, std::string& err) {
    Clear();

    AVFormatContext* in_fmt = nullptr;
    int ret = avformat_open_input(&in_fmt, input_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        err = "avformat_open_input failed: " + AvErr(ret);
        return false;
    }

    ret = avformat_find_stream_info(in_fmt, nullptr);
    if (ret < 0) {
        err = "avformat_find_stream_info failed: " + AvErr(ret);
        avformat_close_input(&in_fmt);
        return false;
    }

    AVPacket pkt;
    av_init_packet(&pkt);

    int64_t packet_index = 0;
    while ((ret = av_read_frame(in_fmt, &pkt)) >= 0) {
        AVStream* in_stream = in_fmt->streams[pkt.stream_index];
        PacketEdit edit;
        edit.packet_index = packet_index;
        edit.stream_index = pkt.stream_index;
        edit.edited_pts = edit.original_pts = pkt.pts * TIME_BASE * av_q2d(in_stream->time_base);
        edit.edited_dts = edit.original_dts = pkt.dts * TIME_BASE * av_q2d(in_stream->time_base);
        edit.duration = pkt.duration * TIME_BASE * av_q2d(in_stream->time_base);
        edit.size = pkt.size;
        edit.flags = pkt.flags;
        edit.pos = pkt.pos;
        stream_index_map_[pkt.stream_index].push_back(packet_index);
        edits_.push_back(edit);
        av_packet_unref(&pkt);
        packet_index++;
    }

    if (ret != AVERROR_EOF) {
        err = "av_read_frame failed: " + AvErr(ret);
        avformat_close_input(&in_fmt);
        Clear();
        return false;
    }

    avformat_close_input(&in_fmt);
    input_path_ = input_path;
    return true;
}

bool FrameEditor::SaveAs(const std::string& output_path, std::string& err) {
    if (input_path_.empty()) {
        err = "No input loaded";
        return false;
    }

    AVFormatContext* in_fmt = nullptr;
    int ret = avformat_open_input(&in_fmt, input_path_.c_str(), nullptr, nullptr);
    if (ret < 0) {
        err = "avformat_open_input failed: " + AvErr(ret);
        return false;
    }

    ret = avformat_find_stream_info(in_fmt, nullptr);
    if (ret < 0) {
        err = "avformat_find_stream_info failed: " + AvErr(ret);
        avformat_close_input(&in_fmt);
        return false;
    }

    AVFormatContext* out_fmt = nullptr;
    ret = avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output_path.c_str());
    if (ret < 0 || !out_fmt) {
        err = "avformat_alloc_output_context2 failed: " + AvErr(ret);
        avformat_close_input(&in_fmt);
        return false;
    }

    for (unsigned int i = 0; i < in_fmt->nb_streams; ++i) {
        AVStream* in_stream = in_fmt->streams[i];
        AVStream* out_stream = avformat_new_stream(out_fmt, nullptr);
        if (!out_stream) {
            err = "avformat_new_stream failed";
            avformat_free_context(out_fmt);
            avformat_close_input(&in_fmt);
            return false;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        if (ret < 0) {
            err = "avcodec_parameters_copy failed: " + AvErr(ret);
            avformat_free_context(out_fmt);
            avformat_close_input(&in_fmt);
            return false;
        }

        out_stream->time_base = in_stream->time_base;
        out_stream->codecpar->codec_tag = 0;
    }

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out_fmt->pb, output_path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            err = "avio_open failed: " + AvErr(ret);
            avformat_free_context(out_fmt);
            avformat_close_input(&in_fmt);
            return false;
        }
    }

    ret = avformat_write_header(out_fmt, nullptr);
    if (ret < 0) {
        err = "avformat_write_header failed: " + AvErr(ret);
        if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&out_fmt->pb);
        }
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return false;
    }

    std::unordered_map<int64_t, const PacketEdit*> edit_map;
    edit_map.reserve(edits_.size());
    for (const auto& e : edits_) {
        edit_map[e.packet_index] = &e;
    }

    AVPacket pkt;
    av_init_packet(&pkt);
    int64_t packet_index = 0;

    while ((ret = av_read_frame(in_fmt, &pkt)) >= 0) {
        AVStream* in_stream = in_fmt->streams[pkt.stream_index];
        AVStream* out_stream = out_fmt->streams[pkt.stream_index];
        auto it = edit_map.find(packet_index++);
        if (it != edit_map.end()) {
            const PacketEdit* e = it->second;
            if (e->deleted) {
                av_packet_unref(&pkt);
                continue;
            }
            pkt.pts = e->edited_pts / av_q2d(out_stream->time_base) / TIME_BASE;
            pkt.dts = e->edited_dts / av_q2d(out_stream->time_base) / TIME_BASE;
            pkt.duration = e->duration / av_q2d(out_stream->time_base) / TIME_BASE;
        }

        // 关键：从输入 time_base 转到输出 time_base
        // av_packet_rescale_ts(&pkt, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        ret = av_interleaved_write_frame(out_fmt, &pkt);
        av_packet_unref(&pkt);

        if (ret < 0) {
            err = "av_interleaved_write_frame failed: " + AvErr(ret);
            break;
        }
    }

    if (ret == AVERROR_EOF) {
        ret = 0;
    }

    if (ret >= 0) {
        ret = av_write_trailer(out_fmt);
        if (ret < 0) {
            err = "av_write_trailer failed: " + AvErr(ret);
        }
    }

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&out_fmt->pb);
    }
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);

    return ret >= 0;
}
