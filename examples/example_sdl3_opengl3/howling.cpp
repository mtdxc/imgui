#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include "howling.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 更新滤波器系数
void NotchFilter::update(int center_freq, float q, float gain_db) {
    center_freq_ = center_freq;
    q_factor_ = q;
    gain_db_ = gain_db;
    bandwidth_ = center_freq / q;
    calculateCoefficients();
}

// 处理一个样本
float NotchFilter::process(float input) {
    if (!enabled_) {
        return input;
    }

    // 直接形式II实现
    float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

    // 更新延迟线
    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = output;

    return output;
}

// 处理一组样本
void NotchFilter::processBlock(float* input, float* output, int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        output[i] = process(input[i]);
    }
}

// 重置滤波器状态
void NotchFilter::reset() {
    x1 = x2 = 0.0f;
    y1 = y2 = 0.0f;
}

// 计算滤波器系数
void NotchFilter::calculateCoefficients() {
    float omega = 2.0f * static_cast<float>(M_PI) * center_freq_ / sample_rate_;
    float alpha = std::sin(omega) / (2.0f * q_factor_);
    float A = std::pow(10.0f, std::abs(gain_db_) / 40.0f);  // 幅度

    if (gain_db_ < 0) {
        // 陷波滤波器 (衰减)
        b0 = 1.0f;
        b1 = -2.0f * std::cos(omega);
        b2 = 1.0f;
        float denom = 1.0f + alpha * A;
        a1 = -2.0f * std::cos(omega) / denom;
        a2 = (1.0f - alpha * A) / denom;

        // 归一化
        float a0 = 1.0f + alpha * A;
        b0 /= a0;
        b1 /= a0;
        b2 /= a0;
        a1 /= a0;
        a2 /= a0;
    } else {
        // 峰值滤波器 (增益)
        b0 = 1.0f + alpha * A;
        b1 = -2.0f * std::cos(omega);
        b2 = 1.0f - alpha * A;
        float denom = 1.0f + alpha;
        a1 = -2.0f * std::cos(omega) / denom;
        a2 = (1.0f - alpha) / denom;

        // 归一化
        float a0 = 1.0f + alpha;
        b0 /= a0;
        b1 /= a0;
        b2 /= a0;
        a1 /= a0;
        a2 /= a0;
    }
}


// 计算FFT (简单的DFT实现，适用于教学)
SimpleFFT::SimpleFFT(int size) : size_(size) {
    // 预计算旋转因子
    twiddle_factors_.resize(size);
    for (int k = 0; k < size; k++) {
        float angle = -2.0f * static_cast<float>(M_PI) * k / size;
        twiddle_factors_[k] = std::complex<float>(std::cos(angle), std::sin(angle));
    }
}

void SimpleFFT::compute(const float* input_real, const float* input_imag, float* output_real, float* output_imag) {
    for (int k = 0; k < size_; k++) {
        std::complex<float> sum(0.0f, 0.0f);
        for (int n = 0; n < size_; n++) {
            std::complex<float> x(input_real[n], input_imag[n]);
            int idx = (n * k) % size_;
            sum += x * twiddle_factors_[idx];
        }
        output_real[k] = sum.real();
        output_imag[k] = sum.imag();
    }
}

// 计算幅度谱
void SimpleFFT::computeMagnitude(const float* real, const float* imag, float* magnitude, int num_points) {
    for (int i = 0; i < num_points; i++) {
        magnitude[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
    }
}

// 检测频谱中的峰值
void PeakDetector::findPeaks(const float* magnitude_spectrum) {
    int half_size = fft_size_ / 2;
    float freq_bin = sample_rate_ / fft_size_;
    int min_bin_distance = static_cast<int>(min_distance_hz_ / freq_bin);

    // 平滑频谱
    for (int i = 0; i < half_size; i++) {
        smoothed_spectrum_[i] = smoothing_factor_ * smoothed_spectrum_[i] +
            (1.0f - smoothing_factor_) * magnitude_spectrum[i];
    }

    peak_count_ = 0;
    std::fill(peak_magnitudes_.begin(), peak_magnitudes_.end(), 0.0f);
    std::fill(peak_indices_.begin(), peak_indices_.end(), 0);
    std::fill(peak_frequencies_.begin(), peak_frequencies_.end(), 0.0f);

    // 检测峰值
    for (int i = min_bin_distance; i < half_size - min_bin_distance; i++) {
        float magnitude = smoothed_spectrum_[i];
        float db = 20.0f * std::log10(magnitude + 1e-10f);

        if (db > threshold_db_) {
            bool is_peak = true;

            // 检查是否为局部最大值
            for (int j = -min_bin_distance; j <= min_bin_distance; j++) {
                if (j != 0 && magnitude <= smoothed_spectrum_[i + j]) {
                    is_peak = false;
                    break;
                }
            }

            if (is_peak) {
                // 避免重复检测
                bool duplicate = false;
                for (int p = 0; p < peak_count_; p++) {
                    if (std::abs(i - peak_indices_[p]) < min_bin_distance) {
                        duplicate = true;
                        // 保留更大的峰值
                        if (magnitude > peak_magnitudes_[p]) {
                            peak_indices_[p] = i;
                            peak_magnitudes_[p] = magnitude;
                            peak_frequencies_[p] = i * freq_bin;
                        }
                        break;
                    }
                }

                if (!duplicate && peak_count_ < static_cast<int>(peak_magnitudes_.size())) {
                    peak_indices_[peak_count_] = i;
                    peak_magnitudes_[peak_count_] = magnitude;
                    peak_frequencies_[peak_count_] = i * freq_bin;
                    peak_count_++;
                }
            }
        }
    }
}

FeedbackSuppressor::FeedbackSuppressor(int sample_rate, int max_filters, int frame_size, int hop_size)
    : sample_rate_(sample_rate), max_filters_(max_filters),
    frame_size_(frame_size), hop_size_(hop_size),
    enabled_(true), suppression_amount_(0.7f),
    default_q_factor_(10.0f), buffer_pos_(0),
    analysis_interval_(2048), analysis_counter_(0) {

    // 初始化滤波器组
    notch_filters_.reserve(max_filters_);
    for (int i = 0; i < max_filters_; i++) {
        notch_filters_.emplace_back(sample_rate_, 1000.0f);
        notch_filters_[i].setEnabled(false);
    }

    // 初始化峰值检测器
    peak_detector_.reset(new PeakDetector(frame_size_, sample_rate_));

    // 初始化FFT处理器
    fft_processor_.reset(new SimpleFFT(frame_size_));

    // 分配缓冲区
    input_buffer_.resize(frame_size_, 0.0f);
    output_buffer_.resize(frame_size_, 0.0f);
    window_.resize(frame_size_, 0.0f);
    fft_real_.resize(frame_size_, 0.0f);
    fft_imag_.resize(frame_size_, 0.0f);
    magnitude_spectrum_.resize(frame_size_ / 2, 0.0f);

    // 创建汉宁窗
    for (int i = 0; i < frame_size_; i++) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (frame_size_ - 1)));
    }
}

// 处理音频帧
void FeedbackSuppressor::processFrame() {
    if (!enabled_) {
        std::fill(output_buffer_.begin(), output_buffer_.end(), 0.0f);
        return;
    }

    // 检查是否需要分析
    analysis_counter_ += frame_size_;
    if (analysis_counter_ >= analysis_interval_) {
        analyzeSpectrum();
        updateNotchFilters();
        analysis_counter_ = 0;
    }

    // 应用陷波滤波器处理当前帧
    applyNotchFilters();
}

// 处理实时音频流
void FeedbackSuppressor::process(const float* input, float* output, int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        // 填充输入缓冲区
        input_buffer_[buffer_pos_] = input[i];
        buffer_pos_++;

        // 当缓冲区满时处理一帧
        if (buffer_pos_ >= frame_size_) {
            processFrame();

            // 输出处理后的音频
            for (int j = 0; j < frame_size_; j++) {
                int output_idx = i - frame_size_ + j + 1;
                if (output_idx >= 0 && output_idx < num_samples) {
                    output[output_idx] = output_buffer_[j];
                }
            }

            // 滑动缓冲区
            std::memmove(input_buffer_.data(),
                input_buffer_.data() + hop_size_,
                (frame_size_ - hop_size_) * sizeof(float));
            buffer_pos_ = frame_size_ - hop_size_;
        }
    }
}

// 处理单一样本（低延迟模式）
float FeedbackSuppressor::processSample(float input) {
    if (!enabled_) {
        return input;
    }

    // 填充缓冲区
    input_buffer_[buffer_pos_] = input;
    buffer_pos_++;

    float output = input;

    // 应用当前活动的滤波器
    for (auto& filter : notch_filters_) {
        if (filter.isEnabled()) {
            output = filter.process(output);
        }
    }

    // 检查是否需要处理新帧
    if (buffer_pos_ >= frame_size_) {
        processFrame();

        // 滑动缓冲区
        std::memmove(input_buffer_.data(),
            input_buffer_.data() + hop_size_,
            (frame_size_ - hop_size_) * sizeof(float));
        buffer_pos_ = frame_size_ - hop_size_;
    }

    return output;
}

// 重置状态
void FeedbackSuppressor::reset() {
    buffer_pos_ = 0;
    analysis_counter_ = 0;
    std::fill(input_buffer_.begin(), input_buffer_.end(), 0.0f);
    std::fill(output_buffer_.begin(), output_buffer_.end(), 0.0f);

    for (auto& filter : notch_filters_) {
        filter.reset();
        filter.setEnabled(false);
    }
}

// 分析频谱
void FeedbackSuppressor::analyzeSpectrum() {
    // 应用窗函数
    for (int i = 0; i < frame_size_; i++) {
        fft_real_[i] = input_buffer_[i] * window_[i];
        fft_imag_[i] = 0.0f;
    }

    // 计算FFT
    std::vector<float> temp_real(frame_size_, 0.0f);
    std::vector<float> temp_imag(frame_size_, 0.0f);
    fft_processor_->compute(fft_real_.data(), fft_imag_.data(),
        temp_real.data(), temp_imag.data());

    // 计算幅度谱
    fft_processor_->computeMagnitude(temp_real.data(), temp_imag.data(),
        magnitude_spectrum_.data(), frame_size_ / 2);

    // 检测峰值
    if (peak_detector_) {
        peak_detector_->findPeaks(magnitude_spectrum_.data());
    }
}

// 更新陷波滤波器
void FeedbackSuppressor::updateNotchFilters() {
    if (!peak_detector_) return;

    int detected_peaks = peak_detector_->getPeakCount();
    int filters_needed = std::min(detected_peaks, max_filters_);
    const auto& peak_frequencies = peak_detector_->getPeakFrequencies();

    // 禁用不需要的滤波器
    for (int i = filters_needed; i < max_filters_; i++) {
        if (notch_filters_[i].isEnabled()) {
            notch_filters_[i].setEnabled(false);
        }
    }

    // 更新或创建滤波器
    for (int i = 0; i < filters_needed; i++) {
        float freq = peak_frequencies[i];
        float attenuation = -20.0f * suppression_amount_;

        if (notch_filters_[i].isEnabled()) {
            // 如果频率变化较大，重新设置滤波器
            if (std::abs(freq - notch_filters_[i].getCenterFrequency()) > 10.0f) {
                notch_filters_[i].update(freq, default_q_factor_, attenuation);
            }
        }
        else {
            // 创建新滤波器
            notch_filters_[i] = NotchFilter(sample_rate_, freq, default_q_factor_, attenuation);
            notch_filters_[i].setEnabled(true);
        }
    }
}

// 应用陷波滤波器
void FeedbackSuppressor::applyNotchFilters() {
    // 复制输入到输出
    std::copy(input_buffer_.begin(), input_buffer_.end(), output_buffer_.begin());

    // 应用所有启用的滤波器
    for (auto& filter : notch_filters_) {
        if (filter.isEnabled()) {
            filter.processBlock(output_buffer_.data(), output_buffer_.data(), frame_size_);
        }
    }
}


// ==================== 音频信号生成器 ====================
class TestSignalGenerator {
private:
    int sample_rate_;

public:
    TestSignalGenerator(int sample_rate = 44100) : sample_rate_(sample_rate) {}

    // 生成包含啸叫的测试信号
    void generateWithFeedback(float* signal, int num_samples,
        const std::vector<float>& feedback_frequencies = { 1000.0f, 2000.0f, 3000.0f },
        float feedback_gain = 0.3f, float base_gain = 0.1f) {

        for (int i = 0; i < num_samples; i++) {
            float t = static_cast<float>(i) / sample_rate_;

            // 基础音频信号
            signal[i] = base_gain * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t) +  // A4音符
                base_gain * 0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 880.0f * t); // A5音符

            // 添加啸叫（逐渐增强）
            float gain_ramp = std::min(1.0f, static_cast<float>(i) / (sample_rate_ * 2.0f));

            for (size_t j = 0; j < feedback_frequencies.size(); j++) {
                float freq = feedback_frequencies[j];
                float freq_gain = feedback_gain * (1.0f - 0.1f * j);  // 不同频率不同增益
                signal[i] += gain_ramp * freq_gain * std::sin(2.0f * static_cast<float>(M_PI) * freq * t);
            }
        }
    }

    // 生成白噪声测试信号
    void generateWhiteNoise(float* signal, int num_samples, float gain = 0.1f) {
        for (int i = 0; i < num_samples; i++) {
            signal[i] = gain * (2.0f * static_cast<float>(rand()) / RAND_MAX - 1.0f);
        }
    }
};

// ==================== 示例使用代码 ====================
int test_howling() {
    std::cout << "C++ 啸叫抑制算法演示" << std::endl;

    // 参数设置
    int sample_rate = 44100;
    int duration_seconds = 5;
    int num_samples = sample_rate * duration_seconds;
    int max_filters = 8;

    // 创建啸叫抑制器
    FeedbackSuppressor suppressor(sample_rate, max_filters);

    // 设置参数
    suppressor.setSuppressionAmount(0.7f);
    suppressor.setQFactor(12.0f);
    suppressor.setPeakThresholdDB(-35.0f);

    // 生成测试信号
    std::vector<float> input_signal(num_samples, 0.0f);
    std::vector<float> output_signal(num_samples, 0.0f);

    TestSignalGenerator generator(sample_rate);
    generator.generateWithFeedback(input_signal.data(), num_samples);

    std::cout << "正在处理音频信号..." << std::endl;

    // 处理音频
    suppressor.process(input_signal.data(), output_signal.data(), num_samples);

    // 计算信号能量
    float input_energy = 0.0f, output_energy = 0.0f;
    for (int i = 0; i < num_samples; i++) {
        input_energy += input_signal[i] * input_signal[i];
        output_energy += output_signal[i] * output_signal[i];
    }

    input_energy /= num_samples;
    output_energy /= num_samples;

    std::cout << "\n处理完成！" << std::endl;
    std::cout << "输入信号能量: " << input_energy << std::endl;
    std::cout << "输出信号能量: " << output_energy << std::endl;
    std::cout << "能量衰减: " << 10.0f * std::log10(output_energy / input_energy) << " dB" << std::endl;
    std::cout << "使用滤波器数量: " << suppressor.getActiveFilterCount() << "/" << max_filters << std::endl;

    // 输出滤波器信息
    std::vector<float> active_freqs = suppressor.getActiveFilterFrequencies();
    if (!active_freqs.empty()) {
        std::cout << "\n活动的陷波滤波器频率:" << std::endl;
        for (size_t i = 0; i < active_freqs.size(); i++) {
            std::cout << "  滤波器 " << i + 1 << ": " << active_freqs[i] << " Hz" << std::endl;
        }
    }

    // 演示实时处理
    std::cout << "\n=== 实时处理演示 ===" << std::endl;

    suppressor.reset();
    suppressor.setEnabled(true);

    // 模拟实时处理
    float test_input[] = { 0.1f, 0.2f, 0.15f, 0.3f, 0.25f, 0.1f, 0.05f, 0.2f };
    float test_output[8];

    for (int i = 0; i < 8; i++) {
        test_output[i] = suppressor.processSample(test_input[i]);
        std::cout << "输入: " << test_input[i] << " -> 输出: " << test_output[i] << std::endl;
    }

    return 0;
}
