#ifndef _HOWLING_H_
#define _HOWLING_H_

#include <memory>
#include <complex>
#include <vector>

class NotchFilter {
private:
    // 滤波器系数
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;   // 分子系数
    float a1 = 0.0f, a2 = 0.0f;       // 分母系数

    // 延迟线
    float x1 = 0.0f, x2 = 0.0f;       // 输入延迟
    float y1 = 0.0f, y2 = 0.0f;       // 输出延迟

    // 滤波器参数
    int center_freq_ = 100;  // 中心频率 (Hz)
    int sample_rate_ = 44100;  // 采样率 (Hz)
    float q_factor_ = 10.0f;     // Q值
    float gain_db_ = -20.0f;      // 陷波深度 (dB)
    int bandwidth_ = 100;    // 带宽 (Hz)

    bool enabled_ = true;       // 滤波器是否启用
    // 计算滤波器系数
    void calculateCoefficients();
public:
    NotchFilter() {}

    NotchFilter(int sample_rate, int center_freq, float q = 10.0f, float gain_db = -20.0f)
        : center_freq_(center_freq), sample_rate_(sample_rate),
          q_factor_(q), gain_db_(gain_db), enabled_(true) {
        reset();
        calculateCoefficients();
    }
    void update(int center_freq, float q, float gain_db);
    float process(float input);
    void processBlock(float* input, float* output, int num_samples);
    void reset();

    // 启用/禁用滤波器
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // 获取参数
    int getCenterFrequency() const { return center_freq_; }
    float getQFactor() const { return q_factor_; }
    float getGainDB() const { return gain_db_; }
    int getBandwidth() const { return bandwidth_; }

    // 设置参数
    void setCenterFrequency(int freq) {
        center_freq_ = freq;
        calculateCoefficients();
    }

    void setQFactor(float q) {
        q_factor_ = q;
        calculateCoefficients();
    }

    void setGainDB(float gain_db) {
        gain_db_ = gain_db;
        calculateCoefficients();
    }
};

// ==================== 简单的FFT类 ====================
class SimpleFFT {
private:
    int size_;
    std::vector<std::complex<float>> twiddle_factors_;

public:
    SimpleFFT(int size);

    // 计算FFT (简单的DFT实现，适用于教学)
    void compute(const float* input_real, const float* input_imag,
        float* output_real, float* output_imag);

    // 计算幅度谱
    void computeMagnitude(const float* real, const float* imag,
        float* magnitude, int num_points);
};

// ==================== 峰值检测器类 ====================
class PeakDetector {
private:
    int fft_size_;
    int sample_rate_;
    float threshold_db_;
    int min_distance_hz_;
    float smoothing_factor_;

    std::vector<float> spectrum_;
    std::vector<float> smoothed_spectrum_;
    std::vector<float> peak_magnitudes_;
    std::vector<int> peak_indices_;
    std::vector<float> peak_frequencies_;
    int peak_count_;

public:
    PeakDetector(int fft_size = 1024, int sample_rate = 44100)
        : fft_size_(fft_size), sample_rate_(sample_rate),
        threshold_db_(-40.0f), min_distance_hz_(50),
        smoothing_factor_(0.9f), peak_count_(0) {
        int half_size = fft_size_ / 2;
        spectrum_.resize(half_size, 0.0f);
        smoothed_spectrum_.resize(half_size, 0.0f);
        peak_magnitudes_.resize(half_size / 2, 0.0f);  // 最多half_size/2个峰值
        peak_indices_.resize(half_size / 2, 0);
        peak_frequencies_.resize(half_size / 2, 0.0f);
    }

    // 检测频谱中的峰值
    void findPeaks(const float* magnitude_spectrum);

    // 获取峰值信息
    int getPeakCount() const { return peak_count_; }
    const std::vector<float>& getPeakFrequencies() const { return peak_frequencies_; }
    const std::vector<float>& getPeakMagnitudes() const { return peak_magnitudes_; }

    // 参数设置
    void setThresholdDB(float threshold) { threshold_db_ = threshold; }
    void setMinDistanceHz(float distance) { min_distance_hz_ = distance; }
    void setSmoothingFactor(float factor) { smoothing_factor_ = factor; }

    float getThresholdDB() const { return threshold_db_; }
    float getMinDistanceHz() const { return min_distance_hz_; }
    float getSmoothingFactor() const { return smoothing_factor_; }
};

// ==================== 啸叫抑制器类 ====================
class FeedbackSuppressor {
private:
    std::vector<NotchFilter> notch_filters_;
    std::unique_ptr<PeakDetector> peak_detector_;
    std::unique_ptr<SimpleFFT> fft_processor_;

    int sample_rate_;
    int max_filters_;
    int frame_size_;
    int hop_size_;

    std::vector<float> input_buffer_;
    std::vector<float> output_buffer_;
    std::vector<float> window_;
    std::vector<float> fft_real_;
    std::vector<float> fft_imag_;
    std::vector<float> magnitude_spectrum_;
    int buffer_pos_;

    int analysis_interval_;
    int analysis_counter_;

    bool enabled_;
    float suppression_amount_;
    float default_q_factor_;

public:
    FeedbackSuppressor(int sample_rate = 44100, int max_filters = 8,
        int frame_size = 1024, int hop_size = 256);

    ~FeedbackSuppressor() = default;

    // 处理音频帧
    void processFrame();

    // 处理实时音频流
    void process(const float* input, float* output, int num_samples);

    // 处理单一样本（低延迟模式）
    float processSample(float input);

    // 重置状态
    void reset();

    // 参数设置
    void setEnabled(bool enabled) { enabled_ = enabled; }
    void setSuppressionAmount(float amount) {
        if (amount < 0.0f) amount = 0.0f;
        if (amount > 1.0f) amount = 1.0f;
        suppression_amount_ = amount;
    }
    float getSuppressionAmount() const { return suppression_amount_; }
    void setQFactor(float q) {
        default_q_factor_ = q;
        updateNotchFilters();
    }
    void setPeakThresholdDB(float threshold) {
        if (peak_detector_) {
            peak_detector_->setThresholdDB(threshold);
        }
    }

    bool isEnabled() const { return enabled_; }
    int getActiveFilterCount() const {
        int count = 0;
        for (const auto& filter : notch_filters_) {
            if (filter.isEnabled()) count++;
        }
        return count;
    }

    // 获取滤波器信息
    std::vector<float> getActiveFilterFrequencies() const {
        std::vector<float> frequencies;
        for (const auto& filter : notch_filters_) {
            if (filter.isEnabled()) {
                frequencies.push_back(filter.getCenterFrequency());
            }
        }
        return frequencies;
    }

private:
    // 分析频谱
    void analyzeSpectrum();

    // 更新陷波滤波器
    void updateNotchFilters();

    // 应用陷波滤波器
    void applyNotchFilters();
};
int test_howling();
#endif // _HOWLING_H_
