// feedback_suppression.h
#ifndef FEEDBACK_SUPPRESSION_H
#define FEEDBACK_SUPPRESSION_H

#include <vector>
#include <complex>
#include <deque>
#include <cmath>

class FeedbackSuppression {
public:
    /**
     * @brief 构造函数
     * @param sample_rate 采样率（Hz）
     * @param fft_size FFT大小，建议1024或2048
     */
    FeedbackSuppression(int sample_rate = 44100, int fft_size = 2048);

    ~FeedbackSuppression();

    /**
     * @brief 处理音频数据
     * @param input 输入音频数据
     * @return 处理后的音频数据
     */
    std::vector<float> process(const std::vector<float>& input);

    /**
     * @brief 实时处理单个音频块
     * @param input 输入音频数据指针
     * @param output 输出音频数据指针
     * @param num_samples 样本数量
     */
    void processBlock(const float* input, float* output, size_t num_samples);

    /**
     * @brief 设置参数
     * @param suppression_strength 抑制强度（0-1）
     * @param threshold_db 检测阈值（dB）
     */
    void setParameters(float suppression_strength = 0.8f, float threshold_db = -12.0f);
    void getParameters(float& suppression_strength, float& threshold_db) const {
        suppression_strength = suppression_strength_;
        threshold_db = threshold_db_;
    }
    float getSuppressionAmount() const { return suppression_strength_; }
    float getThresholdDB() const { return threshold_db_; }

    /**
     * @brief 重置滤波器状态
     */
    void reset();

    void setEnabled(bool v) {enabled_ = v;}
    bool enabled() const {return enabled_;}
private:
    bool enabled_ = true;
    // 内部结构：陷波滤波器
    struct NotchFilter {
        float center_freq;
        float bandwidth;
        float depth;
        int age = 0;

        // 二阶IIR滤波器系数
        float b0 = 1.0f;
        float b1 = 0, b2 = 0;
        float a1 = 0, a2 = 0;

        // 滤波器状态
        float x1 = 0, x2 = 0;
        float y1 = 0, y2 = 0;

        NotchFilter(){}
        NotchFilter(float freq, float bw, float d) : center_freq(freq), bandwidth(bw), depth(d) {}
        float process(float sample);
        void updateCoefficients(float sample_rate);
        void reset() {
            x1 = x2 = 0.0f;
            y1 = y2 = 0.0f;
        }
    };

    // 私有方法
    void analyzeSpectrum(const std::vector<float>& frame);
    void detectFeedbackPeaks(const std::vector<float>& magnitude);
    void updateNotchFilters();
    float applyNotchFilters(float sample);

    // PocketFFT相关函数声明
    void r2cFFT(const std::vector<float>& in, std::vector<std::complex<float>>& out);
    void c2rIFFT(const std::vector<std::complex<float>>& in, std::vector<float>& out);

private:
    // 参数
    int sample_rate_;
    int fft_size_; // 2048
    int hop_size_; // fft_size / 4
    int num_bins_; // fft_size / 2 + 1

    // 处理参数
    float suppression_strength_; // 0.8f
    float threshold_db_; // -12.0f
    float threshold_linear_;

    // FFT相关
    std::vector<float> window_;
    std::vector<float> overlap_buffer_;

    // 频谱分析
    std::vector<float> magnitude_history_;
    std::vector<float> phase_history_;
    std::vector<float> power_spectrum_;
    std::vector<int> peak_history_;

    // 陷波滤波器
    std::vector<NotchFilter> notch_filters_;
    const int max_filters_ = 8;  // 最大陷波滤波器数量
    const int min_fb_age_ = 10;  // 最小啸叫持续时间

    // 统计信息
    std::deque<std::vector<float>> spectrum_buffer_;
    int frame_counter_ = 0;

    // 调试信息
    std::vector<float> detected_frequencies_;
};

#endif // FEEDBACK_SUPPRESSION_H
