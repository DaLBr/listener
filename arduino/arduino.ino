extern "C" {
#include "user_interface.h"
#include "i2s_reg.h"
#include "slc_register.h"
#include "esp8266_peri.h"
void rom_i2c_writeReg_Mask(int, int, int, int, int, int);
}

#define i2c_bbpll                                 0x67
#define i2c_bbpll_en_audio_clock_out              4
#define i2c_bbpll_en_audio_clock_out_msb          7
#define i2c_bbpll_en_audio_clock_out_lsb          7
#define i2c_bbpll_hostid                          4

#define i2c_writeReg_Mask(block, host_id, reg_add, Msb, Lsb, indata) \
  rom_i2c_writeReg_Mask(block, host_id, reg_add, Msb, Lsb, indata)
#define i2c_readReg_Mask(block, host_id, reg_add, Msb, Lsb) \
  rom_i2c_readReg_Mask(block, host_id, reg_add, Msb, Lsb)
#define i2c_writeReg_Mask_def(block, reg_add, indata) \
  i2c_writeReg_Mask(block, block##_hostid, reg_add, reg_add##_msb, reg_add##_lsb, indata)
#define i2c_readReg_Mask_def(block, reg_add) \
  i2c_readReg_Mask(block, block##_hostid, reg_add, reg_add##_msb, reg_add##_lsb)

#define I2SI_DATA       12
#define I2SI_BCK        13
#define I2SI_WS         14

#define RX_NUM          128
#define IIS_RX_BUF_LEN  2048

typedef struct {
  uint32_t blocksize:12;
  uint32_t datalen:12;
  uint32_t unused:5;
  uint32_t sub_sof:1;
  uint32_t eof:1;
  uint32_t owner:1;

  uint32_t buf_ptr;
  uint32_t next_link_ptr;
} sdio_queue_t;


uint32_t i2s_rx_buff1[IIS_RX_BUF_LEN / 4];
uint32_t i2s_rx_buff2[IIS_RX_BUF_LEN / 4];

uint32_t buffer1[IIS_RX_BUF_LEN / 2];
uint32_t buffer2[IIS_RX_BUF_LEN / 2];

volatile uint32_t rx_buff1_cnt = 0;
volatile uint32_t rx_buff2_cnt = 0;
volatile bool rx_buff1_flag = false;
volatile bool rx_buff2_flag = false;

sdio_queue_t i2s_rx_queue1, i2s_rx_queue2;

void i2s_init();
void slc_init();
void slc_isr(void *para);
void load_buffer1_1(uint32_t *buffer, uint32_t length);
void load_buffer1_2(uint32_t *buffer, uint32_t length);
void load_buffer2_1(uint32_t *buffer, uint32_t length);
void load_buffer2_2(uint32_t *buffer, uint32_t length);

void
setup()
{
  i2s_init();
  slc_init();

  Serial.begin(115200);
  Serial.println("Initialised");
}

void
loop()
{
  // Find the DMA which sends the interrupt signal
  if (rx_buff1_flag) {
    // Replace data in the buff
    if ((rx_buff1_cnt % 2) == 0) {
      load_buffer2_1(i2s_rx_buff1, IIS_RX_BUF_LEN / 4);
    }
    else if ((rx_buff1_cnt % 2) == 1) {
      load_buffer1_1(i2s_rx_buff1, IIS_RX_BUF_LEN / 4);
    }
    rx_buff1_flag = false;
  }
  if (rx_buff2_flag) {
    if ((rx_buff2_cnt % 2) == 0) {
      load_buffer2_2(i2s_rx_buff2, IIS_RX_BUF_LEN / 4);
    }
    else if ((rx_buff2_cnt % 2) == 1) {
      load_buffer1_2(i2s_rx_buff2, IIS_RX_BUF_LEN / 4);
    }
    rx_buff2_flag = false;
  }
}

void
i2s_init()
{
  // CONFIG I2S RX PIN FUNC
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_I2SI_DATA);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_I2SI_BCK);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_I2SI_WS);

  // Enable a 160MHz clock to i2s subsystem
  i2c_writeReg_Mask_def(i2c_bbpll, i2c_bbpll_en_audio_clock_out, 1);

  // Reset I2S
  CLEAR_PERI_REG_MASK(I2SCONF, I2S_I2S_RESET_MASK);
  SET_PERI_REG_MASK(I2SCONF, I2S_I2S_RESET_MASK);
  CLEAR_PERI_REG_MASK(I2SCONF, I2S_I2S_RESET_MASK);

  // Enable FIFO in i2s module
  SET_PERI_REG_MASK(I2S_FIFO_CONF, I2S_I2S_DSCR_EN);

  // Set I2S_FIFO
  // Set RX data size to 24 bit
  SET_PERI_REG_MASK(I2S_FIFO_CONF, (I2S_I2S_RX_FIFO_MOD << I2S_I2S_RX_FIFO_MOD_S));

  // Set I2S_CHAN
  // Set  RX channel mode to 2 channel
  SET_PERI_REG_MASK(I2SCONF_CHAN, (I2S_RX_CHAN_MOD << I2S_RX_CHAN_MOD_S));

  // Set RX eof num
  WRITE_PERI_REG(I2SRXEOF_NUM, RX_NUM);

  // RX master mode,
  // MSB_shift, right_first, MSB_right,
  // Use I2S clock divider to produce a 32KHz Sample Rate
  CLEAR_PERI_REG_MASK(I2SCONF, I2S_RECE_SLAVE_MOD |
            (I2S_BITS_MOD << I2S_BITS_MOD_S) |
            (I2S_BCK_DIV_NUM << I2S_BCK_DIV_NUM_S) |
            (I2S_CLKM_DIV_NUM << I2S_CLKM_DIV_NUM_S));

  SET_PERI_REG_MASK(I2SCONF, I2S_RIGHT_FIRST | I2S_MSB_RIGHT | I2S_TRANS_SLAVE_MOD |
            I2S_RECE_MSB_SHIFT | I2S_TRANS_MSB_SHIFT |
            ((26 & I2S_BCK_DIV_NUM) << I2S_BCK_DIV_NUM_S) |
            ((4 & I2S_CLKM_DIV_NUM) << I2S_CLKM_DIV_NUM_S) |
            (8 << I2S_BITS_MOD_S));

  // Clear int
  SET_PERI_REG_MASK(I2SINT_CLR,
    I2S_I2S_RX_REMPTY_INT_CLR |
    I2S_I2S_RX_WFULL_INT_CLR |
    I2S_I2S_PUT_DATA_INT_CLR |
    I2S_I2S_TAKE_DATA_INT_CLR
  );
  CLEAR_PERI_REG_MASK(I2SINT_CLR,
    I2S_I2S_RX_REMPTY_INT_CLR |
    I2S_I2S_RX_WFULL_INT_CLR |
    I2S_I2S_PUT_DATA_INT_CLR |
    I2S_I2S_TAKE_DATA_INT_CLR
  );

  // Enable int
  SET_PERI_REG_MASK(I2SINT_ENA,
    I2S_I2S_RX_REMPTY_INT_ENA |
    I2S_I2S_RX_WFULL_INT_ENA |
    I2S_I2S_RX_TAKE_DATA_INT_ENA
  );

  // Start receiver
  SET_PERI_REG_MASK(I2SCONF, I2S_I2S_RX_START);
}

// Initialize the SLC module for DMA function
// https://github.com/CHERTS/esp8266-devkit/
void
slc_init()
{
  // Reset DMA
  SET_PERI_REG_MASK(SLC_CONF0, SLC_RXLINK_RST);
  CLEAR_PERI_REG_MASK(SLC_CONF0, SLC_RXLINK_RST);

  // Enable and configure DMA
  CLEAR_PERI_REG_MASK(SLC_CONF0, (SLC_MODE << SLC_MODE_S));
  SET_PERI_REG_MASK(SLC_CONF0, (1 << SLC_MODE_S));
  SET_PERI_REG_MASK(SLC_RX_DSCR_CONF, SLC_INFOR_NO_REPLACE | SLC_TOKEN_NO_REPLACE);
  CLEAR_PERI_REG_MASK(SLC_RX_DSCR_CONF, SLC_RX_FILL_EN | SLC_RX_EOF_MODE | SLC_RX_FILL_MODE);

  ETS_SLC_INTR_ATTACH(slc_isr, NULL);
  // Enable sdio operation intr
  WRITE_PERI_REG(SLC_INT_ENA, SLC_INTEREST_EVENT);
  // Clear sdio initial random active intr signal
  WRITE_PERI_REG(SLC_INT_CLR, 0xffffffff);
  // Enable sdio intr in cpu
  ETS_SLC_INTR_ENABLE();
}

void
slc_isr(void *para)
{
  uint32_t slc_intr_status;

  slc_intr_status = READ_PERI_REG(SLC_INT_STATUS);
  Serial.println("Data received");
  if (slc_intr_status == 0) {
    return;
  }
  WRITE_PERI_REG(SLC_INT_CLR, 0xffffffff);

  if (slc_intr_status & SLC_RX_EOF_INT_ST) {
    if (READ_PERI_REG(SLC_RX_EOF_DES_ADDR) == (((uint32_t)&i2s_rx_queue1))) {
      rx_buff1_flag = true;
      rx_buff1_cnt++;
    }
    else if (READ_PERI_REG(SLC_RX_EOF_DES_ADDR) == (((uint32_t)&i2s_rx_queue2))) {
      rx_buff2_flag = true;
      rx_buff2_cnt++;
    }
  }
}

// Load data into buffer
void
load_buffer1_1(uint32_t *buffer, uint32_t length)
{
  uint32_t i;
  uint32_t *pbuff = buffer;

  for (i = 0; i < length; i++) {
    *pbuff = buffer1[i];
    pbuff++;
  }
}

void
load_buffer1_2(uint32_t *buffer, uint32_t length)
{
  uint32_t i;
  uint32_t *pbuff = buffer;

  for (i = 0; i < length; i++) {
    *pbuff = buffer1[length + i];
    pbuff++;
  }
}

void
load_buffer2_1(uint32_t *buffer, uint32_t length)
{
  uint32_t i;
  uint32_t *pbuff = buffer;

  for (i = 0; i < length; i++) {
    *pbuff = buffer2[i];
    pbuff++;
  }
}

void
load_buffer2_2(uint32_t *buffer, uint32_t length)
{
  uint32_t i;
  uint32_t *pbuff = buffer;

  for (i = 0; i < length; i++) {
    *pbuff = buffer2[length + i];
    pbuff++;
  }
}
