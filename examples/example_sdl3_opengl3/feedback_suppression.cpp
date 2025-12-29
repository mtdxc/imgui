// feedback_suppression.cpp
#include <cstring>
#include <iostream>
#include <algorithm>
#include <queue>
#include "feedback_suppression.h"
#include "pocketfft_hdronly.h"
using namespace pocketfft;
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace std;

// 陷波滤波器实现
float FeedbackSuppression::NotchFilter::process(float sample) {
    // 直接形式II实现
    float output = b0 * sample + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

    // 更新状态
    x2 = x1;
    x1 = sample;
    y2 = y1;
    y1 = output;

    return output;
}

void FeedbackSuppression::NotchFilter::updateCoefficients(float sample_rate) {
    float w0 = 2.0f * M_PI * center_freq / sample_rate;
    float alpha = sinf(w0) * sinhf(logf(2.0f) / 2.0f * bandwidth * w0 / sinf(w0));
    float cos_w0 = cosf(w0);

    // 二阶陷波滤波器系数
    float A = powf(10.0f, depth / 40.0f);  // 深度转换为线性增益
    float beta = sqrtf(A) * alpha;

    b0 = 1.0f;
    b1 = -2.0f * cos_w0;
    b2 = 1.0f;
    float a0 = 1.0f + beta;
    a1 = -2.0f * cos_w0 / a0;
    a2 = (1.0f - beta) / a0;

    // 归一化
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    // a1, a2 already divided by a0
}

// 主类实现
FeedbackSuppression::FeedbackSuppression(int sample_rate, int fft_size)
    : sample_rate_(sample_rate),
      fft_size_(fft_size),
      hop_size_(fft_size / 4),
      num_bins_(fft_size / 2 + 1) {

    // 初始化阈值
    setParameters();

    // 初始化窗函数（汉宁窗）
    window_.resize(fft_size_);
    for (int i = 0; i < fft_size_; ++i) {
        window_[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (fft_size_ - 1)));
    }

    // 初始化重叠缓冲区
    overlap_buffer_.resize(hop_size_, 0.0f);

    // 初始化频谱历史
    magnitude_history_.resize(num_bins_, 0.0f);
    phase_history_.resize(num_bins_, 0.0f);
    power_spectrum_.resize(num_bins_, 0.0f);
    peak_history_.resize(num_bins_, 0);

    // 初始化频谱缓冲区
    spectrum_buffer_.resize(5, vector<float>(num_bins_, 0.0f));

    cout << "Feedback Suppression initialized with FFT size: "
         << fft_size_ << ", samplerate: " << sample_rate_ << endl;
}

FeedbackSuppression::~FeedbackSuppression() {
}

void FeedbackSuppression::setParameters(float suppression_strength, float threshold_db) {
    suppression_strength_ = max(0.0f, min(1.0f, suppression_strength));
    threshold_db_ = threshold_db;
    threshold_linear_ = powf(10.0f, threshold_db_ / 20.0f);
}

void FeedbackSuppression::r2cFFT(const vector<float>& in, vector<complex<float>>& out) {
    size_t n = in.size();
    out.resize(n / 2 + 1);

    // 执行实数FFT
    shape_t shape{n};
    stride_t stride_in{sizeof(float)};
    stride_t stride_out{sizeof(complex<float>)};
    shape_t axes{0};

    r2c(shape, stride_in, stride_out, axes, FORWARD,
        in.data(), out.data(), 1.0f);
}

void FeedbackSuppression::c2rIFFT(const vector<complex<float>>& in, vector<float>& out) {
    size_t n = (in.size() - 1) * 2;
    out.resize(n);

    // 执行复数到实数IFFT
    shape_t shape{n};
    stride_t stride_in{sizeof(complex<float>)};
    stride_t stride_out{sizeof(float)};
    shape_t axes{0};

    c2r(shape, stride_in, stride_out, axes, BACKWARD,
        in.data(), out.data(), 1.0f / n);
}

void FeedbackSuppression::reset() {
    // 重置滤波器状态
    for (auto& filter : notch_filters_) {
        filter.reset();
    }

    // 重置历史数据
    fill(overlap_buffer_.begin(), overlap_buffer_.end(), 0.0f);
    fill(magnitude_history_.begin(), magnitude_history_.end(), 0.0f);
    fill(phase_history_.begin(), phase_history_.end(), 0.0f);
    fill(power_spectrum_.begin(), power_spectrum_.end(), 0.0f);
    fill(peak_history_.begin(), peak_history_.end(), 0);

    frame_counter_ = 0;
    detected_frequencies_.clear();
}

vector<float> FeedbackSuppression::process(const vector<float>& input) {
    if (input.empty()||!enabled_) return input;

    vector<float> output;
    
    // 分帧处理
    size_t pos = 0;
    while (pos + fft_size_ <= input.size()) {
        // 提取当前帧
        vector<float> frame(input.begin() + pos, input.begin() + pos + fft_size_);

        // 应用窗函数
        for (int i = 0; i < fft_size_; ++i) {
            frame[i] *= window_[i];
        }

        // 频谱分析
        analyzeSpectrum(frame);

        // 检测啸叫
        detectFeedbackPeaks(magnitude_history_);

        // 更新陷波滤波器
        updateNotchFilters();

        // 时域处理（应用陷波滤波器）
        vector<float> processed_frame(fft_size_);
        for (int i = 0; i < fft_size_; ++i) {
            processed_frame[i] = applyNotchFilters(frame[i]);
        }

        // 应用窗函数（用于重叠相加）
        for (int i = 0; i < fft_size_; ++i) {
            processed_frame[i] *= window_[i];
        }

        // 重叠相加
        if (output.size() < pos + fft_size_) {
            output.resize(pos + fft_size_, 0.0f);
        }

        for (int i = 0; i < fft_size_; ++i) {
            output[pos + i] += processed_frame[i];
        }

        pos += hop_size_;
    }

    return output;
}

void FeedbackSuppression::processBlock(const float* input, float* output, size_t num_samples) {
    if (num_samples != fft_size_) {
        cerr << "Error: Input block size must match FFT size" << endl;
        return;
    }
    if (!enabled_) {
        if (output != input) {
            memcpy(output, input, num_samples * sizeof(float));
        }
        return ;
    }
    // 复制输入数据
    vector<float> frame(input, input + fft_size_);

    // 应用窗函数
    for (int i = 0; i < fft_size_; ++i) {
        frame[i] *= window_[i];
    }

    // 频谱分析
    analyzeSpectrum(frame);

    // 检测啸叫峰值
    detectFeedbackPeaks(magnitude_history_);

    // 更新陷波滤波器
    updateNotchFilters();

    // 应用陷波滤波器
    for (int i = 0; i < fft_size_; ++i) {
        output[i] = applyNotchFilters(frame[i]);
    }
}

void FeedbackSuppression::analyzeSpectrum(const vector<float>& frame) {
    // 计算FFT
    vector<complex<float>> spectrum;
    r2cFFT(frame, spectrum);

    // 更新幅度和相位历史
    for (int i = 0; i < num_bins_; ++i) {
        magnitude_history_[i] = abs(spectrum[i]);
        phase_history_[i] = arg(spectrum[i]);
        power_spectrum_[i] = norm(spectrum[i]);
    }

    // 更新频谱缓冲区
    spectrum_buffer_.pop_front();
    spectrum_buffer_.push_back(magnitude_history_);

    frame_counter_++;
}

void FeedbackSuppression::detectFeedbackPeaks(const vector<float>& magnitude) {
    // 更新峰值历史
    for (int i = 0; i < num_bins_; ++i) {
        float current_mag = magnitude[i];
        float freq = i * sample_rate_ / (float)fft_size_;

        // 只关注可能的啸叫频率范围（通常200Hz-8kHz）
        if (freq < 200.0f || freq > 8000.0f) {
            peak_history_[i] = 0;
            continue;
        }

        // 检查是否是局部峰值
        bool is_peak = true;
        if (i > 0 && current_mag <= magnitude[i-1]) 
            is_peak = false;
        if (i < num_bins_-1 && current_mag <= magnitude[i+1]) 
            is_peak = false;

        if (is_peak) {
            // 计算峰值显著性
            float avg_surround = 0.0f;
            int count = 0;

            // 检查周围5个bin的平均值
            for (int j = max(0, i-5); j <= min(num_bins_-1, i+5); j++) {
                if (j != i) {
                    avg_surround += magnitude[j];
                    count++;
                }
            }
            avg_surround /= count;

            // 如果峰值显著高于周围频率
            float peak_ratio = current_mag / (avg_surround + 1e-10f);
            float peak_db = 20.0f * log10f(peak_ratio);

            if (peak_db > threshold_db_ && peak_ratio > threshold_linear_ * 2.0f) {
                peak_history_[i]++;

                // 如果连续检测到多次，确认为啸叫
                if (peak_history_[i] >= min_fb_age_) {
                    // 添加到检测到的频率列表
                        detected_frequencies_.push_back(freq);

                    // 限制列表大小
                    if (detected_frequencies_.size() > 20) {
                        detected_frequencies_.erase(detected_frequencies_.begin());
                    }
                }
            } else if (peak_history_[i]) {
                peak_history_[i]--;
            }
        } else if (peak_history_[i]) {
            peak_history_[i]--;
        }
    }
}

void FeedbackSuppression::updateNotchFilters() {
    // 找出需要处理的啸叫频率
    vector<pair<float, float>> detected_peaks; // <频率, 强度>

    for (int i = 0; i < num_bins_; ++i) {
        if (peak_history_[i] >= min_fb_age_) {
            float freq = i * sample_rate_ / (float)fft_size_;
            float strength = magnitude_history_[i];
            detected_peaks.emplace_back(freq, strength);
        }
    }

    // 按强度排序
    sort(detected_peaks.begin(), detected_peaks.end(),
         [](const pair<float, float>& a, const pair<float, float>& b) {
             return a.second > b.second;
         });

    // 更新陷波滤波器
    for (auto& filter : notch_filters_) {
        filter.age++;
    }

    // 移除旧的滤波器
    auto it = remove_if(notch_filters_.begin(), notch_filters_.end(),
                       [](const NotchFilter& f) { return f.age > 500; });
    if (it != notch_filters_.end()) {
        notch_filters_.erase(it, notch_filters_.end());
    }

    // 添加新的滤波器（如果需要）
    for (size_t i = 0; i < min(detected_peaks.size(), (size_t)max_filters_); ++i) {
        float freq = detected_peaks[i].first;
        float strength = detected_peaks[i].second;

        // 检查是否已经有相近频率的滤波器
        bool exists = false;
        for (auto& filter : notch_filters_) {
            if (abs(filter.center_freq - freq) < 10.0f) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            // 根据强度设置深度
            float depth = -24.0f * suppression_strength_;
            NotchFilter new_filter(freq, 1.0f, depth); // 1.0倍频程带宽
            new_filter.updateCoefficients(sample_rate_);
                notch_filters_.push_back(new_filter);

            cout << "Added notch filter at " << freq << " Hz, depth: " << depth << ", " << strength
                 << " dB, now have " << notch_filters_.size() << " filters." << endl;
        }
    }

    // 限制滤波器数量
    if (notch_filters_.size() > max_filters_) {
        // 按年龄排序，移除最老的
        sort(notch_filters_.begin(), notch_filters_.end(),
             [](const NotchFilter& a, const NotchFilter& b) { return a.age > b.age; });
        notch_filters_.resize(max_filters_);
    }
}

float FeedbackSuppression::applyNotchFilters(float sample) {
    float result = sample;

    // 串联所有陷波滤波器
    for (auto& filter : notch_filters_) {
        result = filter.process(result);
    }

    return result;
}
