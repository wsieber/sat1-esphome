#include "tas2780.h"

#include <algorithm>

#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tas2780 {

static const char *const TAG = "tas2780";

static const uint8_t TAS2780_PAGE_SELECT = 0x00;  // Page Select

/* PAGE 0*/
static const uint8_t TAS2780_SW_RESET = 0x01;   // Software Reset
static const uint8_t TAS2780_MODE_CTRL = 0x02;  // Device operational mode
static const uint8_t TAS2780_MODE_CTRL_BOP_SRC__PVDD_UVLO = 0x80;
static const uint8_t TAS2780_MODE_CTRL_MODE_MASK = 0x07;
static const uint8_t TAS2780_MODE_CTRL_MODE__ACTIVE = 0x00;
static const uint8_t TAS2780_MODE_CTRL_MODE__ACTIVE_MUTED = 0x01;
static const uint8_t TAS2780_MODE_CTRL_MODE__SFTW_SHTDWN = 0x02;

static const uint8_t TAS2780_CHNL_0 = 0x03;  // Y Bridge and Channel settings
static const uint8_t TAS2780_CHNL_0_CDS_MODE_SHIFT = 6;
static const uint8_t TAS2780_CHNL_0_CDS_MODE_MASK = (0x03 << TAS2780_CHNL_0_CDS_MODE_SHIFT);
static const uint8_t TAS2780_CHNL_0_AMP_LEVEL_SHIFT = 1;
static const uint8_t TAS2780_CHNL_0_AMP_LEVEL_MASK = (0x1F) << TAS2780_CHNL_0_AMP_LEVEL_SHIFT;

static const uint8_t TAS2780_DC_BLK0 = 0x04;  // SAR Filter and DC Path Blocker
static const uint8_t TAS2780_DC_BLK0_VBAT1S_MODE_SHIFT = 7;
static const uint8_t TAS2780_DC_BLK1 = 0x05;    // Record DC Blocker
static const uint8_t TAS2780_MISC_CFG1 = 0x06;  // Misc Configuration 1
static const uint8_t TAS2780_MISC_CFG2 = 0x07;  // Misc Configuration 2
static const uint8_t TAS2780_TDM_CFG0 = 0x08;   // TDM Configuration 0
static const uint8_t TAS2780_TDM_CFG1 = 0x09;   // TDM Configuration 1

static const uint8_t TAS2780_TDM_CFG2 = 0x0A;  // TDM Configuration 2
static const uint8_t TAS2780_TDM_CFG2_RX_SCFG_SHIFT = 4;
static const uint8_t TAS2780_TDM_CFG2_RX_SCFG_MASK = (3 << TAS2780_TDM_CFG2_RX_SCFG_SHIFT);
static const uint8_t TAS2780_TDM_CFG2_RX_SCFG__STEREO_DWN_MIX = (3 << TAS2780_TDM_CFG2_RX_SCFG_SHIFT);
static const uint8_t TAS2780_TDM_CFG2_RX_SCFG__MONO_LEFT = (1 << TAS2780_TDM_CFG2_RX_SCFG_SHIFT);
static const uint8_t TAS2780_TDM_CFG2_RX_SCFG__MONO_RIGHT = (2 << TAS2780_TDM_CFG2_RX_SCFG_SHIFT);
static const uint8_t TAS2780_TDM_CFG2_RX_WLEN_SHIFT = 2;
static const uint8_t TAS2780_TDM_CFG2_RX_WLEN_MASK = (3 << TAS2780_TDM_CFG2_RX_WLEN_SHIFT);
static const uint8_t TAS2780_TDM_CFG2_RX_WLEN__16BIT = (0 << TAS2780_TDM_CFG2_RX_WLEN_SHIFT);
static const uint8_t TAS2780_TDM_CFG2_RX_WLEN__24BIT = (2 << TAS2780_TDM_CFG2_RX_WLEN_SHIFT);
static const uint8_t TAS2780_TDM_CFG2_RX_WLEN__32BIT = (3 << TAS2780_TDM_CFG2_RX_WLEN_SHIFT);
static const uint8_t TAS2780_TDM_CFG2_RX_SLEN_MASK = (3 << 0);
static const uint8_t TAS2780_TDM_CFG2_RX_SLEN__32BIT = 2;

static const uint8_t TAS2780_LIM_MAX_ATTN = 0x0B;  // Limiter
static const uint8_t TAS2780_TDM_CFG3 = 0x0C;      // TDM Configuration 3
static const uint8_t TAS2780_TDM_CFG4 = 0x0D;      // TDM Configuration 4
static const uint8_t TAS2780_TDM_CFG5 = 0x0E;      // TDM Configuration 5
static const uint8_t TAS2780_TDM_CFG6 = 0x0F;      // TDM Configuration 6
static const uint8_t TAS2780_TDM_CFG7 = 0x10;      // TDM Configuration 7
static const uint8_t TAS2780_TDM_CFG8 = 0x11;      // TDM Configuration 8
static const uint8_t TAS2780_TDM_CFG9 = 0x12;      // TDM Configuration 9
static const uint8_t TAS2780_TDM_CFG10 = 0x13;     // TDM Configuration 10
static const uint8_t TAS2780_TDM_CFG11 = 0x14;     // TDM Configuration 11
static const uint8_t TAS2780_ICC_CNFG2 = 0x15;     // ICC Mode
static const uint8_t TAS2780_TDM_CFG12 = 0x16;     // TDM Configuration 12
static const uint8_t TAS2780_ICLA_CFG0 = 0x17;     // Inter Chip Limiter Alignment 0
static const uint8_t TAS2780_ICLA_CFG1 = 0x18;     // Inter Chip Gain Alignment 1
static const uint8_t TAS2780_DG_0 = 0x19;          // Diagnostic Signal

static const uint8_t TAS2780_DVC = 0x1A;        // Digital Volume Control
static const uint8_t TAS2780_LIM_CFG0 = 0x1B;   // Limiter Configuration 0
static const uint8_t TAS2780_LIM_CFG1 = 0x1C;   // Limiter Configuration 1
static const uint8_t TAS2780_BOP_CFG0 = 0x1D;   // Brown Out Prevention 0
static const uint8_t TAS2780_BOP_CFG1 = 0x1E;   // Brown Out Prevention 1
static const uint8_t TAS2780_BOP_CFG2 = 0x1F;   // Brown Out Prevention 2
static const uint8_t TAS2780_BOP_CFG3 = 0x20;   // Brown Out Prevention 3
static const uint8_t TAS2780_BOP_CFG4 = 0x21;   // Brown Out Prevention 4
static const uint8_t TAS2780_BOP_CFG5 = 0x22;   // BOP Configuration 5
static const uint8_t TAS2780_BOP_CFG6 = 0x23;   // Brown Out Prevention 6
static const uint8_t TAS2780_BOP_CFG7 = 0x24;   // Brown Out Prevention 7
static const uint8_t TAS2780_BOP_CFG8 = 0x25;   // Brown Out Prevention 8
static const uint8_t TAS2780_BOP_CFG9 = 0x26;   // Brown Out Prevention 9
static const uint8_t TAS2780_BOP_CFG10 = 0x27;  // BOP Configuration 10
static const uint8_t TAS2780_BOP_CFG11 = 0x28;  // Brown Out Prevention 11
static const uint8_t TAS2780_BOP_CFG12 = 0x29;  // Brown Out Prevention 12
static const uint8_t TAS2780_BOP_CFG13 = 0x2A;  // Brown Out Prevention 13
static const uint8_t TAS2780_BOP_CFG14 = 0x2B;  // Brown Out Prevention 14
static const uint8_t TAS2780_BOP_CFG15 = 0x2C;  // BOP Configuration 15
static const uint8_t TAS2780_BOP_CFG17 = 0x2D;  // Brown Out Prevention 17
static const uint8_t TAS2780_BOP_CFG18 = 0x2E;  // Brown Out Prevention 18
static const uint8_t TAS2780_BOP_CFG19 = 0x2F;  // Brown Out Prevention 19
static const uint8_t TAS2780_BOP_CFG20 = 0x30;  // Brown Out Prevention 20
static const uint8_t TAS2780_BOP_CFG21 = 0x31;  // BOP Configuration 21
static const uint8_t TAS2780_BOP_CFG22 = 0x32;  // Brown Out Prevention 22
static const uint8_t TAS2780_BOP_CFG23 = 0x33;  // Lowest PVDD Measured
static const uint8_t TAS2780_BOP_CFG24 = 0x34;  // Lowest BOP Attack Rate
static const uint8_t TAS2780_NG_CFG0 = 0x35;    // Noise Gate 0
static const uint8_t TAS2780_NG_CFG1 = 0x36;    // Noise Gate 1
static const uint8_t TAS2780_LVS_CFG0 = 0x37;   // Low Voltage Signaling
static const uint8_t TAS2780_DIN_PD = 0x38;     // Digital Input Pin Pull Down

/* Interrupts */
static const uint8_t TAS2780_INT_MASK0 = 0x3B;    // Interrupt Mask 0
static const uint8_t TAS2780_INT_MASK1 = 0x3C;    // Interrupt Mask 1
static const uint8_t TAS2780_INT_MASK4 = 0x3D;    // Interrupt Mask 4
static const uint8_t TAS2780_INT_MASK2 = 0x40;    // Interrupt Mask 2
static const uint8_t TAS2780_INT_MASK3 = 0x41;    // Interrupt Mask 3
static const uint8_t TAS2780_INT_LIVE0 = 0x42;    // Live Interrupt Read-back 0
static const uint8_t TAS2780_INT_LIVE1 = 0x43;    // Live Interrupt Read-back 1
static const uint8_t TAS2780_INT_LIVE1_0 = 0x44;  // Live Interrupt Read-back 1_0
static const uint8_t TAS2780_INT_LIVE2 = 0x47;    // Live Interrupt Read-back 2
static const uint8_t TAS2780_INT_LIVE3 = 0x48;    // Live Interrupt Read-back 3
static const uint8_t TAS2780_INT_LTCH0 = 0x49;    // Latched Interrupt Read-back 0
static const uint8_t TAS2780_INT_LTCH1 = 0x4A;    // Latched Interrupt Read-back 1
static const uint8_t TAS2780_INT_LTCH1_0 = 0x4B;  // Latched Interrupt Read-back 1_0
static const uint8_t TAS2780_INT_LTCH2 = 0x4F;    // Latched Interrupt Read-back 2
static const uint8_t TAS2780_INT_LTCH3 = 0x50;    // Latched Interrupt Read-back 3
static const uint8_t TAS2780_INT_LTCH4 = 0x51;    // Latched Interrupt Read-back 4

static const uint8_t TAS2780_VBAT_MSB = 0x52;     // SAR VBAT1S 0
static const uint8_t TAS2780_VBAT_LSB = 0x53;     // SAR VBAT1S 1
static const uint8_t TAS2780_PVDD_MSB = 0x54;     // SAR PVDD 0
static const uint8_t TAS2780_PVDD_LSB = 0x55;     // SAR PVDD 1
static const uint8_t TAS2780_TEMP = 0x56;         // SAR ADC Conversion 2
static const uint8_t TAS2780_INT_CLK_CFG = 0x5C;  // Clock Setting and IRQZ
static const uint8_t TAS2780_MISC_CFG3 = 0x5D;    // Misc Configuration 3
static const uint8_t TAS2780_CLOCK_CFG = 0x60;    // Clock Configuration
static const uint8_t TAS2780_IDLE_IND = 0x63;     // Idle channel current optimization
static const uint8_t TAS2780_SAR_SAMP = 0x64;     // SAR Sampling Time
static const uint8_t TAS2780_MISC_CFG4 = 0x65;    // Misc Configuration 4
static const uint8_t TAS2780_TG_CFG0 = 0x67;      // Tone Generator
static const uint8_t TAS2780_CLK_CFG = 0x68;      // Detect Clock Ration and Sample Rate
static const uint8_t TAS2780_LV_EN_CFG = 0x6A;    // Class-D and LVS Delays
static const uint8_t TAS2780_NG_CFG2 = 0x6B;      // Noise Gate 2
static const uint8_t TAS2780_NG_CFG3 = 0x6C;      // Noise Gate 3
static const uint8_t TAS2780_NG_CFG4 = 0x6D;      // Noise Gate 4
static const uint8_t TAS2780_NG_CFG5 = 0x6E;      // Noise Gate 5
static const uint8_t TAS2780_NG_CFG6 = 0x6F;      // Noise Gate 6
static const uint8_t TAS2780_NG_CFG7 = 0x70;      // Noise Gate 7
static const uint8_t TAS2780_PVDD_UVLO = 0x71;    // UVLO Threshold
static const uint8_t TAS2780_DMD = 0x73;          // DAC Modulator Dither
static const uint8_t TAS2780_I2C_CKSUM = 0x7E;    // I2C Checksum
static const uint8_t TAS2780_BOOK = 0x7F;         // Device Book

/* PAGE 0x01*/
static const uint8_t TAS2780_INIT_0 = 0x17;       // Initialization
static const uint8_t TAS2780_LSR = 0x19;          // Modulation
static const uint8_t TAS2780_INIT_1 = 0x21;       // Initialization
static const uint8_t TAS2780_INIT_2 = 0x35;       // Initialization
static const uint8_t TAS2780_INT_LDO = 0x36;      // Internal LDO Setting
static const uint8_t TAS2780_SDOUT_HIZ_1 = 0x3D;  // Slots Control
static const uint8_t TAS2780_SDOUT_HIZ_2 = 0x3E;  // Slots Control
static const uint8_t TAS2780_SDOUT_HIZ_3 = 0x3F;  // Slots Control
static const uint8_t TAS2780_SDOUT_HIZ_4 = 0x40;  // Slots Control
static const uint8_t TAS2780_SDOUT_HIZ_5 = 0x41;  // Slots Control
static const uint8_t TAS2780_SDOUT_HIZ_6 = 0x42;  // Slots Control
static const uint8_t TAS2780_SDOUT_HIZ_7 = 0x43;  // Slots Control
static const uint8_t TAS2780_SDOUT_HIZ_8 = 0x44;  // Slots Control
static const uint8_t TAS2780_SDOUT_HIZ_9 = 0x45;  // Slots Control
static const uint8_t TAS2780_TG_EN = 0x47;        // Thermal Detection Enable
static const uint8_t TAS2780_EDGE_CTRL = 0x4C;    // Slew rate control

/* PAGE 0x04*/
static const uint8_t TAS2780_DG_DC_VAL1 = 0x08;    // Diagnostic DC Level
static const uint8_t TAS2780_DG_DC_VAL2 = 0x09;    // Diagnostic DC Level
static const uint8_t TAS2780_DG_DC_VAL3 = 0x0A;    // Diagnostic DC Level
static const uint8_t TAS2780_DG_DC_VAL4 = 0x0B;    // Diagnostic DC Level
static const uint8_t TAS2780_LIM_TH_MAX1 = 0x0C;   // Limiter Maximum Threshold
static const uint8_t TAS2780_LIM_TH_MAX2 = 0x0D;   // Limiter Maximum Threshold
static const uint8_t TAS2780_LIM_TH_MAX3 = 0x0E;   // Limiter Maximum Threshold
static const uint8_t TAS2780_LIM_TH_MAX4 = 0x0F;   // Limiter Maximum Threshold
static const uint8_t TAS2780_LIM_TH_MIN1 = 0x10;   // Limiter Minimum Threshold
static const uint8_t TAS2780_LIM_TH_MIN2 = 0x11;   // Limiter Minimum Threshold
static const uint8_t TAS2780_LIM_TH_MIN3 = 0x12;   // Limiter Minimum Threshold
static const uint8_t TAS2780_LIM_TH_MIN4 = 0x13;   // Limiter Minimum Threshold
static const uint8_t TAS2780_LIM_INF_PT1 = 0x14;   // Limiter Inflection Point
static const uint8_t TAS2780_LIM_INF_PT2 = 0x15;   // Limiter Inflection Point
static const uint8_t TAS2780_LIM_INF_PT3 = 0x16;   // Limiter Inflection Point
static const uint8_t TAS2780_LIM_INF_PT4 = 0x17;   // Limiter Inflection Point
static const uint8_t TAS2780_LIM_SLOPE1 = 0x18;    // Limiter Slope
static const uint8_t TAS2780_LIM_SLOPE2 = 0x19;    // Limiter Slope
static const uint8_t TAS2780_LIM_SLOPE3 = 0x1A;    // Limiter Slope
static const uint8_t TAS2780_LIM_SLOPE4 = 0x1B;    // Limiter Slope
static const uint8_t TAS2780_TF_HLD1 = 0x1C;       // TFB Maximum Hold
static const uint8_t TAS2780_TF_HLD2 = 0x1D;       // TFB Maximum Hold
static const uint8_t TAS2780_TF_HLD3 = 0x1E;       // TFB Maximum Hold
static const uint8_t TAS2780_TF_HLD4 = 0x1F;       // TFB Maximum Hold
static const uint8_t TAS2780_TF_RLS1 = 0x20;       // TFB Release Rate
static const uint8_t TAS2780_TF_RLS2 = 0x21;       // TFB Release Rate
static const uint8_t TAS2780_TF_RLS3 = 0x22;       // TFB Release Rate
static const uint8_t TAS2780_TF_RLS4 = 0x23;       // TFB Release Rate
static const uint8_t TAS2780_TF_SLOPE1 = 0x24;     // TFB Limiter Slope
static const uint8_t TAS2780_TF_SLOPE2 = 0x25;     // TFB Limiter Slope
static const uint8_t TAS2780_TF_SLOPE3 = 0x26;     // TFB Limiter Slope
static const uint8_t TAS2780_TF_SLOPE4 = 0x27;     // TFB Limiter Slope
static const uint8_t TAS2780_TF_TEMP_TH1 = 0x28;   // TFB Threshold
static const uint8_t TAS2780_TF_TEMP_TH2 = 0x29;   // TFB Threshold
static const uint8_t TAS2780_TF_TEMP_TH3 = 0x2A;   // TFB Threshold
static const uint8_t TAS2780_TF_TEMP_TH4 = 0x2B;   // TFB Threshold
static const uint8_t TAS2780_TF_MAX_ATTN1 = 0x2C;  // TFB Gain Reduction
static const uint8_t TAS2780_TF_MAX_ATTN2 = 0x2D;  // TFB Gain Reduction
static const uint8_t TAS2780_TF_MAX_ATTN3 = 0x2E;  // TFB Gain Reduction
static const uint8_t TAS2780_TF_MAX_ATTN4 = 0x2F;  // TFB Gain Reduction
static const uint8_t TAS2780_LD_CFG0 = 0x40;       // Load Diagnostics Resistance Upper Threshold
static const uint8_t TAS2780_LD_CFG1 = 0x41;       // Load Diagnostics Resistance Upper Threshold
static const uint8_t TAS2780_LD_CFG2 = 0x42;       // Load Diagnostics Resistance Upper Threshold
static const uint8_t TAS2780_LD_CFG3 = 0x43;       // Load Diagnostics Resistance Upper Threshold
static const uint8_t TAS2780_LD_CFG4 = 0x44;       // Load Diagnostics Resistance Lower Threshold
static const uint8_t TAS2780_LD_CFG5 = 0x45;       // Load Diagnostics Resistance Lower Threshold
static const uint8_t TAS2780_LD_CFG6 = 0x46;       // Load Diagnostics Resistance Lower Threshold
static const uint8_t TAS2780_LD_CFG7 = 0x47;       // Load Diagnostics Resistance Lower Threshold
static const uint8_t TAS2780_CLD_EFF_1 = 0x48;     // Class D Efficiency
static const uint8_t TAS2780_CLD_EFF_2 = 0x49;     // Class D Efficiency
static const uint8_t TAS2780_CLD_EFF_3 = 0x4A;     // Class D Efficiency
static const uint8_t TAS2780_CLD_EFF_4 = 0x4B;     // Class D Efficiency
static const uint8_t TAS2780_LDG_RES1 = 0x4C;      // Load Diagnostics Resistance Value
static const uint8_t TAS2780_LDG_RES2 = 0x4D;      // Load Diagnostics Resistance Value
static const uint8_t TAS2780_LDG_RES3 = 0x4E;      // Load Diagnostics Resistance Value
static const uint8_t TAS2780_LDG_RES4 = 0x4F;      // Load Diagnostics Resistance Value

/* PAGE 0x0FD*/
static const uint8_t TAS2780_INIT_3 = 0x3E;  // Initialization

static const uint8_t TAS2780_INT_LTCH0_IR_OT = (1 << 0);     // over temp error
static const uint8_t TAS2780_INT_LTCH0_IR_OC = (1 << 1);     // over current error
static const uint8_t TAS2780_INT_LTCH0_IR_TDMCE = (1 << 2);  // TDM_CLOCK_ERROR
static const uint8_t TAS2780_INT_LTCH0_IR_LIMA = (1 << 3);   // limiter active
static const uint8_t TAS2780_INT_LTCH0_IR_PBIP = (1 << 4);   // PVDD below limiter inflection point
static const uint8_t TAS2780_INT_LTCH0_IR_LIMMA = (1 << 5);  // limiter max attenuation
static const uint8_t TAS2780_INT_LTCH0_IR_BOPIH = (1 << 6);  // BOP infinite hold
static const uint8_t TAS2780_INT_LTCH0_IR_BOPM = (1 << 7);   // due to bop mute

static const uint8_t TAS2780_INT_LTCH1_IR_VBATLIM = (1 << 0);  // Gain Limiter interrupt
static const uint8_t TAS2780_INT_LTCH1_IR_LDMODE = (3 << 3);   // Load Diagnostic mode fault status
static const uint8_t TAS2780_INT_LTCH1_IR_LDC = (1 << 5);      // Load diagnostic completion
static const uint8_t TAS2780_INT_LTCH1_IR_OTPCRC = (1 << 6);   // OTP CRC error flag

static const uint8_t TAS2780_INT_LTCH1_0_IR_VBAT1S_UVLO = (1 << 5);  // VBAT1S Under Voltage
static const uint8_t TAS2780_INT_LTCH1_0_IR_PLL_CLK = (1 << 7);      // Internal PLL Clock Error

static const uint8_t TAS2780_INT_LTCH2_IR_PUVLO = (1 << 0);   // PVDD UVLO
static const uint8_t TAS2780_INT_LTCH2_IR_LDO_OL = (1 << 1);  // Internal VBAT1S LDO Over Load
static const uint8_t TAS2780_INT_LTCH2_IR_LDO_OV = (1 << 2);  // Internal VBAT1S LDO Over Voltage
static const uint8_t TAS2780_INT_LTCH2_IR_LDO_UV = (1 << 3);  // Internal VBAT1S LDO Under Voltage

static const uint8_t POWER_MODES[4][2] = {
    {2, 0},  // PWR_MODE0: CDS_MODE=10, VBAT1S_MODE=0
    {0, 0},  // PWR_MODE1: CDS_MODE=00, VBAT1S_MODE=0
    {3, 1},  // PWR_MODE2: CDS_MODE=11, VBAT1S_MODE=1
    {1, 0},  // PWR_MODE3: CDS_MODE=01, VBAT1S_MODE=0
};

uint8_t get_channel_select_reg_val(ChannelSelect channel) {
  switch (channel) {
    case MONO_DWN_MIX:
      return TAS2780_TDM_CFG2_RX_SCFG__STEREO_DWN_MIX;
    case LEFT_CHANNEL:
      return TAS2780_TDM_CFG2_RX_SCFG__MONO_LEFT;
    case RIGHT_CHANNEL:
      return TAS2780_TDM_CFG2_RX_SCFG__MONO_RIGHT;
  }
  return TAS2780_TDM_CFG2_RX_SCFG__STEREO_DWN_MIX;
}

static inline uint32_t volts_to_lim_q18(float v_peak) {
  if (!std::isfinite(v_peak) || v_peak < 0.0f) v_peak = 0.0f;
  // Prevent overflow into nonsense if caller passes something huge
  // (2^18 scaling; 0xFFFFFFFF corresponds to ~16383.999 V, unrealistic)
  constexpr float kScale = static_cast<float>(1u << 18);
  const double scaled = static_cast<double>(v_peak) * static_cast<double>(kScale);
  const double rounded = std::round(scaled);
  const double clamped = std::clamp(rounded, 0.0, static_cast<double>(0xFFFFFFFFu));
  return static_cast<uint32_t>(clamped);
}

void TAS2780::setup() {
  this->init();
  // set to software shutdown
  this->reg(TAS2780_MODE_CTRL) =
      (TAS2780_MODE_CTRL_BOP_SRC__PVDD_UVLO & ~TAS2780_MODE_CTRL_MODE_MASK) | TAS2780_MODE_CTRL_MODE__SFTW_SHTDWN;
}

void TAS2780::init() {
  // select page 0
  this->reg(TAS2780_PAGE_SELECT) = 0x00;

  // software reset
  this->reg(TAS2780_SW_RESET) = 0x01;

  uint8_t chd1 = this->reg(0x05).get();
  uint8_t chd2 = this->reg(0x68).get();
  uint8_t chd3 = this->reg(0x02).get();

  if (chd1 == 0x41) {
    ESP_LOGD(TAG, "TAS2780 chip found.");
    ESP_LOGD(TAG, "Reg 0x68: %d.", chd2);
    ESP_LOGD(TAG, "Reg 0x02: %d.", chd3);
  } else {
    ESP_LOGD(TAG, "TAS2780 chip not found.");
    this->mark_failed();
    return;
  }

  this->reg(TAS2780_PAGE_SELECT) = 0x00;
  this->reg(TAS2780_TDM_CFG5) = 0x44;  // TDM tx vsns transmit enable with slot 4
  this->reg(TAS2780_TDM_CFG6) = 0x40;  // TDM tx isns transmit enable with slot 0

  this->reg(TAS2780_PAGE_SELECT) = 0x01;
  this->reg(TAS2780_LSR) = 0x00;     // LSR Mode
  this->reg(TAS2780_INIT_0) = 0xC8;  // SARBurstMask=0, CMP_HYST_LP=1
  this->reg(TAS2780_INIT_1) = 0x00;  // Disable Comparator Hysteresis
  this->reg(TAS2780_INIT_2) = 0x74;  // Noise minimized

  this->reg(TAS2780_PAGE_SELECT) = 0xFD;
  this->reg(0x0D) = 0x0D;            // Access Page 0xFD
  this->reg(TAS2780_INIT_3) = 0x4a;  // Optimal Dmin
  this->reg(0x0D) = 0x00;            // Remove access Page 0xFD

  // When Y bridge is used (eg. PWR_MODE1) PVDD UVLO threshold needs to be set 2.5 V above VBAT1S level.
  //  UVLO = 1.753V + val * 0.332V
  // this->reg(TAS2780_PVDD_UVLO) = 0x12; //PVDD UVLO set to 7.73V
  this->reg(TAS2780_PAGE_SELECT) = 0x00;
  this->reg(TAS2780_PVDD_UVLO) = 0x03;  // PVDD UVLO set to 2.76V

  // Set interrupt masks
  this->reg(TAS2780_PAGE_SELECT) = 0x00;
  // mask VBAT1S Under Voltage
  this->reg(TAS2780_INT_MASK4) = 0xFF;
  // mask all PVDD and VBAT1S interrupts
  this->reg(TAS2780_INT_MASK2) = 0xFF;
  this->reg(TAS2780_INT_MASK3) = 0xFF;
  this->reg(TAS2780_INT_MASK1) = 0xFF;

  // set interrupt to trigger For
  // 0h : On any unmasked live interrupts
  // 3h : 2 - 4 ms every 4 ms on any unmasked latched
  uint8_t reg_0x5c = this->reg(TAS2780_INT_CLK_CFG).get();
  this->reg(TAS2780_INT_CLK_CFG) = (reg_0x5c & ~0x03) | 0x00;

  this->update_settings();
}

void TAS2780::activate(uint8_t power_mode) {
  ESP_LOGD(TAG, "Activating TAS2780 (PWR_MODE:%d)", power_mode);
  // clear interrupt latches
  this->reg(TAS2780_INT_CLK_CFG) = 0x19 | (1 << 2);
  if (power_mode != this->power_mode_) {
    this->power_mode_ = power_mode;
    this->init();
    this->write_mute_();
  }
  // activate
  this->reg(TAS2780_MODE_CTRL) =
      (TAS2780_MODE_CTRL_BOP_SRC__PVDD_UVLO & ~TAS2780_MODE_CTRL_MODE_MASK) | TAS2780_MODE_CTRL_MODE__ACTIVE;
}

void TAS2780::deactivate() {
  ESP_LOGD(TAG, "Dectivating TAS2780");
  // set to software shutdown
  this->reg(TAS2780_MODE_CTRL) =
      (TAS2780_MODE_CTRL_BOP_SRC__PVDD_UVLO & ~TAS2780_MODE_CTRL_MODE_MASK) | TAS2780_MODE_CTRL_MODE__SFTW_SHTDWN;
}

void TAS2780::reset() {
  this->init();
  this->activate(this->power_mode_);
}


void TAS2780::log_error_states() {
  const uint8_t latched_its = this->reg(TAS2780_INT_LTCH0).get();
  // Temperature
  if (latched_its & TAS2780_INT_LTCH0_IR_OT) {
    ESP_LOGE(TAG, "Over temperature error!");
  }
  // Over Current
  if (latched_its & TAS2780_INT_LTCH0_IR_OC) {
    ESP_LOGE(TAG, "Over current error!");
  }

  // TDM CLOCK
  if (latched_its & TAS2780_INT_LTCH0_IR_TDMCE) {
    ESP_LOGE(TAG, "TDM Clock Error!");
  }

  if (latched_its & TAS2780_INT_LTCH0_IR_LIMA) {
    ESP_LOGE(TAG, " limiter active error!");
  }

  if (latched_its & TAS2780_INT_LTCH0_IR_PBIP) {
    ESP_LOGE(TAG, "PVDD below limiter inflection point!");
  }

  if (latched_its & TAS2780_INT_LTCH0_IR_LIMMA) {
    ESP_LOGE(TAG, "Limiter max attenuation!");
  }

  if (latched_its & TAS2780_INT_LTCH0_IR_BOPIH) {
    ESP_LOGE(TAG, "BOP infinite hold!");
  }

  if (latched_its & TAS2780_INT_LTCH0_IR_BOPM) {
    ESP_LOGE(TAG, "BOP Mute!");
  }

  const uint8_t latched1_its = this->reg(TAS2780_INT_LTCH1).get();

  if (latched1_its & TAS2780_INT_LTCH1_IR_VBATLIM) {
    ESP_LOGE(TAG, "Gain Limiter interrupt!");
  }

  if (latched1_its & TAS2780_INT_LTCH1_IR_LDMODE) {
    ESP_LOGE(TAG, "Load Diagnostic mode fault status!");
  }

  if (latched1_its & TAS2780_INT_LTCH1_IR_LDC) {
    ESP_LOGE(TAG, "Load diagnostic completion!");
  }

  if (latched1_its & TAS2780_INT_LTCH1_IR_OTPCRC) {
    ESP_LOGE(TAG, "OTP CRC error flag!");
  }

  const uint8_t latched1_0_its = this->reg(TAS2780_INT_LTCH1_0).get();
  if (latched1_0_its & TAS2780_INT_LTCH1_0_IR_VBAT1S_UVLO) {
    ESP_LOGE(TAG, "VBAT1S Under Voltage!");
  }

  if (latched1_0_its & TAS2780_INT_LTCH1_0_IR_PLL_CLK) {
    ESP_LOGE(TAG, "Internal PLL Clock Error!");
  }

  const uint8_t latched2_its = this->reg(TAS2780_INT_LTCH2).get();
  if (latched2_its & TAS2780_INT_LTCH2_IR_PUVLO) {
    ESP_LOGE(TAG, "PVDD UVLO!");
  }
  if (latched2_its & TAS2780_INT_LTCH2_IR_LDO_OL) {
    ESP_LOGE(TAG, "Internal VBAT1S LDO Over Load!");
  }
  if (latched2_its & TAS2780_INT_LTCH2_IR_LDO_OV) {
    ESP_LOGE(TAG, "Internal VBAT1S LDO Over Voltage!");
  }
  if (latched2_its & TAS2780_INT_LTCH2_IR_LDO_UV) {
    ESP_LOGE(TAG, "Internal VBAT1S LDO Under Voltage!");
  }
}

void TAS2780::loop() {
  static uint32_t last_call = millis();
  if (millis() - last_call > 1000) {
    last_call = millis();
    uint8_t curr_mode = this->reg(TAS2780_MODE_CTRL).get() & 7;
    if (curr_mode == 2) {
      ESP_LOGD(TAG, "Current Mode: SOFTWARE_SHUTDOWN (PWR_MODE: %d)", this->power_mode_);
      this->log_error_states();
    }
  }
}

void TAS2780::dump_config() {}

bool TAS2780::set_mute_off() {
  this->is_muted_ = false;
  return this->write_mute_();
}

bool TAS2780::set_mute_on() {
  this->is_muted_ = true;
  return this->write_mute_();
}

bool TAS2780::set_volume(float volume) {
  this->volume_ = clamp<float>(volume, 0.0, 1.0);
  return this->write_volume_();
}

bool TAS2780::is_muted() { return this->is_muted_; }

float TAS2780::volume() { return this->volume_; }

bool TAS2780::write_mute_() {
  if (this->is_muted_) {
    this->reg(TAS2780_DVC) = 0xC9;
  } else {
    this->write_volume_();
  }
  return true;
}

void TAS2780::update_dvc_volume_range_(float dvc_max_db){
  ESP_LOGD(TAG, "Update max volume range to %4.2f", dvc_max_db);
  this->min_attenuation_db_ = -1 * dvc_max_db;
}

static inline float clamp01(float x) { return clamp<float>(x, 0.0f, 1.0f); }
static inline float loudness_curve(float x, float gamma) {
  x = clamp01(x);
  gamma = std::max(0.1f, gamma);
  return std::pow(x, gamma);
}

bool TAS2780::write_volume_() {
  /*
  V_{AMP} = INPUT + A_{DVC} + A_{AMP}

  V_{AMP} is the amplifier output voltage in dBV
  INPUT: digital input amplitude as a number of dB with respect to 0 dBFS
  A_{DVC}: is the digital volume control setting as a number of dB (default 0 dB)
  A_{AMP}: the amplifier output level setting as a number of dBV

  DVC_LVL[7:0] :            0dB to -100dB [0x00, 0xC8] c8 = 200
  */
  
  const float u = clamp01(this->volume_);
  float span = this->vol_range_max_ - this->vol_range_min_;
  if (span < 0.0f) span = 0.0f;
  float x = this->vol_range_min_ + u * span;
  x = clamp01(x);
  
  constexpr float kGamma = 2.0f; 
  const float y = loudness_curve(x, kGamma);
  const float attn_min_db = clamp<float>(this->min_attenuation_db_, 0.0f, 100.0f); // loudest allowed
  const float attn_max_db = 60.0f;                                                // quietest

  const float attn_db = attn_max_db - y * (attn_max_db - attn_min_db);
  const uint8_t dvc = clamp<uint8_t>(
  static_cast<uint8_t>(std::lround(attn_db * 2.0f)), 0, 0xC8);
  
   ESP_LOGD(TAG,
        "Vol u=%0.3f range=[%0.3f..%0.3f] x=%0.3f gamma=%0.2f y=%0.3f attn=%5.2fdB DVC=0x%02X",
           u, this->vol_range_min_, this->vol_range_max_, x, kGamma, y, attn_db, dvc);

  this->reg(TAS2780_DVC) = dvc;
  return true;
}


void TAS2780::update_settings(){
  this->set_and_write_power_mode_();
  
  const float v_cap_dbV = this->calc_vcap_dBV_();
  ESP_LOGD(TAG, "New upper limit: %2.4f", v_cap_dbV );
  
  // amp level
  const float aamp_dBV = this->calc_and_write_amp_level_(v_cap_dbV);
  
  // limiter
  this->set_and_write_limiter_(v_cap_dbV);
  
  //dvc max
  const float dvc_max_db = std::min(0.f, v_cap_dbV - aamp_dBV);
  this->update_dvc_volume_range_(dvc_max_db);

  // CHANNEL_SELECT
  this->reg(TAS2780_TDM_CFG2) = (get_channel_select_reg_val(this->selected_channel_) | TAS2780_TDM_CFG2_RX_WLEN__32BIT |
                                 TAS2780_TDM_CFG2_RX_SLEN__32BIT);
}



float TAS2780::calc_vcap_dBV_() const {
  const float Rl   = static_cast<float>(static_cast<uint8_t>(this->speaker_impedance_));
  const float Pspk = static_cast<float>(this->speaker_power_);
  
  // ---- Speaker limit (watts -> Vrms -> dBV)
  const float Vspk_rms = std::sqrt(std::max(0.0f, Pspk * Rl));
  const float vmax_spk_dBV = 20.0f * std::log10(std::max(1e-6f, Vspk_rms));
  ESP_LOGD(TAG, "New speaker (%4.2fOhm, %4.2fW) limit: %4.2f", Rl, Pspk, vmax_spk_dBV);

  const float Vs   = static_cast<float>(static_cast<uint8_t>(this->supply_voltage_));
  const float Imax = std::max(0.0f, this->supply_max_current_);
  constexpr float kEta = 0.85f;
  
  const float Pin_max = Vs * Imax;
  const float Pout_I  = Pin_max * kEta;
  const float VlimI_rms = std::sqrt(std::max(0.0f, Pout_I * Rl));
  const float VlimI_dBV = 20.0f * std::log10(std::max(1e-6f, VlimI_rms));
  ESP_LOGD(TAG, "Supply (%4.2fV, %4.2fI) limit: %4.2fdBV %4.2fVrms", Vs, Imax, VlimI_dBV, VlimI_rms);

  // ---- Supply voltage / clipping limit (datasheet Eq(2))
  // TAS2780 datasheet provides typical RFET values; RP is board-specific.
  constexpr float kRp = 0.20f; // ohms, conservative default
 
  const float Rfet = (this->power_mode_ == 0) ? 0.50f : 0.25f;
  const float Rtot = Rl + kRp + Rfet;

  const float Vpk_max   = Vs * (Rl / std::max(1e-6f, Rtot)); // peak
  const float VlimV_rms = Vpk_max / std::sqrt(2.0f);         // sine RMS
  const float VlimV_dBV = 20.0f * std::log10(std::max(1e-6f, VlimV_rms));
  
  ESP_LOGD(TAG, "clipping limit: (%4.2fVpk) limit: %4.2fdBV", Vpk_max, VlimV_dBV);
  const float vmax_supply_dBV = std::min(VlimI_dBV, VlimV_dBV);
  
  // ---- Final cap
  constexpr float kAmpMax_dBV = 21.0f;
  constexpr float kHeadroom_dB = 1.0f;
  const float v_cap_db = std::min({vmax_spk_dBV, vmax_supply_dBV, kAmpMax_dBV}) - kHeadroom_dB;
  
  return v_cap_db;
}




float TAS2780::calc_and_write_amp_level_(float v_cap_dBV){
  constexpr float kAmpMin_dBV = 11.0f;
  constexpr float kAmpMax_dBV = 21.0f;
  constexpr float kStep_dB = 0.5f;
  
  float aamp_dbV = std::clamp(v_cap_dBV, kAmpMin_dBV, kAmpMax_dBV);
  // Quantize to AMP_LVL steps (0.5 dB)
  const float steps_f = std::round((aamp_dbV - kAmpMin_dBV) / kStep_dB);
  
  const int code = static_cast<int>(std::clamp(
      steps_f, 0.0f, (kAmpMax_dBV - kAmpMin_dBV) / kStep_dB));
  
  // Write code to register
  uint8_t reg_val = this->reg(TAS2780_CHNL_0).get();
  reg_val &= ~TAS2780_CHNL_0_AMP_LEVEL_MASK;
  reg_val |= code << TAS2780_CHNL_0_AMP_LEVEL_SHIFT;
  this->reg(TAS2780_CHNL_0) = reg_val;
  ESP_LOGD(TAG, "Setting AMP_LEVEL to %4.2f dBV", steps_f * kStep_dB + kAmpMin_dBV);  
  return steps_f * kStep_dB + kAmpMin_dBV;
}

void TAS2780::set_and_write_limiter_(float v_cap_dbV){
  const float Vrms = std::pow(10.0f, v_cap_dbV / 20.0f);
  const float vpk_max = Vrms * std::sqrt(2.0f);
  
  if (this->power_mode_ == 0){
    this->set_limiter_mode0_fixed_(vpk_max);
  } else if (this->power_mode_ == 2) {
    const float pvdd_nom = static_cast<float>(static_cast<uint8_t>(this->supply_voltage_));
    const float headroom_v = 1.0f;
    const float vpk_inf = std::min(pvdd_nom, vpk_max + headroom_v);
    this->set_limiter_mode2_tracking_(vpk_max, -1, vpk_inf);
  }
}



void TAS2780::set_and_write_power_mode_() {
  // PWR_MODE0: PVDD is the only supply used to deliver output power. VBAT external
  // PWR_MODE1: VBAT1S is used to deliver output power based on level and headroom configured.
  //            When audio signal crosses a programmed threshold Class-D output is switched over PVDD
  // PWR_MODE2: PVDD is the only supply. VBAT1S is delivered by an internal LDO and used to supply at
  //            signals close to idle channel levels. When audio signal levels crosses -100dBFS (default),
  //            Class-D output switches to PVDD.
  // PWR_MODE3: The device can be forced to work out of a low power rail mode of operation.
  
  const uint8_t Vs = static_cast<uint8_t>(this->supply_voltage_);
  if (Vs >= 9 ){
    this->power_mode_ = 2;
  } else {
    this->power_mode_ = 0;
  }
  
  const uint8_t power_mode = this->power_mode_;
  assert(power_mode < 4);
  uint8_t chnl_0 = this->reg(TAS2780_CHNL_0).get();
  this->reg(TAS2780_CHNL_0) =
      (chnl_0 & ~TAS2780_CHNL_0_CDS_MODE_MASK) | (POWER_MODES[power_mode][0] << TAS2780_CHNL_0_CDS_MODE_SHIFT);
  uint8_t dc_blk0 = this->reg(TAS2780_DC_BLK0).get();
  this->reg(TAS2780_DC_BLK0) = (dc_blk0 & ~(1 << TAS2780_DC_BLK0_VBAT1S_MODE_SHIFT)) |
                               (POWER_MODES[power_mode][1] << TAS2780_DC_BLK0_VBAT1S_MODE_SHIFT);
}


  void TAS2780::configure_STL_(
      float th_max_vpk,
      bool enable,
      uint8_t max_attn_db,
      uint8_t attack_code,
      uint8_t hold_code,
      uint8_t release_code,
      bool pause_during_bop,
      bool headroom_enable,
      float th_min_vpk,
      float inf_pt_vpk
  ) {
    // --- 1) Thresholds live on PAGE 0x04
    this->reg(TAS2780_PAGE_SELECT) = 0x04;

    // LIM_TH_MAX[31:0] = round(V * 2^18), big-endian across *_1..*_4
    this->write_u32_be_(TAS2780_LIM_TH_MAX1, volts_to_lim_q18(th_max_vpk));

    if (th_min_vpk >= 0.0f) {
      this->write_u32_be_(TAS2780_LIM_TH_MIN1, volts_to_lim_q18(th_min_vpk));
    }
    if (inf_pt_vpk >= 0.0f) {
      this->write_u32_be_(TAS2780_LIM_INF_PT1, volts_to_lim_q18(inf_pt_vpk));
    }

    // Back to PAGE 0
    this->reg(TAS2780_PAGE_SELECT) = 0x00;

    // --- 2) Limiter dynamics/config live on PAGE 0x00

    // LIM_MAX_ATTN upper nibble encodes 1..15 dB as 0..0xE (0=1dB ... 0xE=15dB)
    max_attn_db = std::clamp<uint8_t>(max_attn_db, 1, 15);
    const uint8_t lim_max_attn_nib = static_cast<uint8_t>(max_attn_db - 1);
    this->reg(TAS2780_LIM_MAX_ATTN) = static_cast<uint8_t>(lim_max_attn_nib << 4);

    // LIM_CFG0: [5]=LIM_HR_EN, [4:1]=LIM_ATK_RT, [0]=LIM_EN
    attack_code &= 0x0F;
    uint8_t lim_cfg0 = 0;
    if (headroom_enable) lim_cfg0 |= (1u << 5);
    lim_cfg0 |= static_cast<uint8_t>(attack_code << 1);
    if (enable) lim_cfg0 |= 1u;
    this->reg(TAS2780_LIM_CFG0) = lim_cfg0;

    // LIM_CFG1: [7]=LIM_PDB, [6:3]=LIM_RLS_RT, [2:0]=LIM_HLD_TM
    release_code &= 0x0F;
    hold_code &= 0x07;
    uint8_t lim_cfg1 = 0;
    if (pause_during_bop) lim_cfg1 |= (1u << 7);
    lim_cfg1 |= static_cast<uint8_t>(release_code << 3);
    lim_cfg1 |= hold_code;
    this->reg(TAS2780_LIM_CFG1) = lim_cfg1;
  }

  
  void TAS2780::set_limiter_mode0_fixed_(float vpk_cap) {
    // Fast-ish attack, modest hold, medium release, plenty of attenuation room.
    this->configure_STL_(
      vpk_cap,
      /*enable=*/true,
      /*max_attn_db=*/12,
      /*attack_code=*/0x03,   // example: 160 us/dB (see datasheet table)
      /*hold_code=*/0x02,     // example: 25 ms
      /*release_code=*/0x06,  // example: 128 ms/dB
      /*pause_during_bop=*/true,
      /*headroom_enable=*/true,
      /*th_min_vpk=*/-1.0f,
      /*inf_pt_vpk=*/-1.0f
    );
  }

  void TAS2780::set_limiter_mode2_tracking_(float vpk_max, float vpk_min, float vpk_inflect) {
    // Similar attack, longer release to avoid audible “gain riding”.
    this->configure_STL_(
      vpk_max,
      /*enable=*/true,
      /*max_attn_db=*/12,
      /*attack_code=*/0x03,
      /*hold_code=*/0x03,     // example: 50 ms
      /*release_code=*/0x08,  // example: 512 ms/dB
      /*pause_during_bop=*/true,
      /*headroom_enable=*/true,
      /*th_min_vpk=*/vpk_min,
      /*inf_pt_vpk=*/vpk_inflect
    );
  }

  void TAS2780::write_u32_be_(uint8_t reg_msb, uint32_t v) {
    this->reg(reg_msb + 0) = static_cast<uint8_t>((v >> 24) & 0xFF);
    this->reg(reg_msb + 1) = static_cast<uint8_t>((v >> 16) & 0xFF);
    this->reg(reg_msb + 2) = static_cast<uint8_t>((v >>  8) & 0xFF);
    this->reg(reg_msb + 3) = static_cast<uint8_t>((v >>  0) & 0xFF);
  }




}  // namespace tas2780
}  // namespace esphome