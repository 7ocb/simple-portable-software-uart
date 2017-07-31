
#ifndef IS_BIT_ON_RX_SET 
#error IS_BIT_ON_RX_SET is not set
#endif

#ifndef START_UART_TIMER 
#error START_UART_TIMER is not set
#endif

#ifndef STOP_UART_TIMER 
#error STOP_UART_TIMER is not set
#endif

#ifndef START_LISTEN_RX_CHANGE 
#error START_LISTEN_RX_CHANGE is not set
#endif

#ifndef STOP_LISTEN_RX_CHANGE 
#error STOP_LISTEN_RX_CHANGE is not set
#endif

#ifndef TX_DOWN 
#error TX_DOWN is not set
#endif

#ifndef TX_UP 
#error TX_UP is not set
#endif

#ifndef BYTE_SENDING_DONE_CALLBACK
#define BYTE_SENDING_DONE_CALLBACK uart_byte_sent
#endif

#ifndef UART_TIMER_CALLBACK_NAME 
#define UART_TIMER_CALLBACK_NAME uart_timer_event
#endif

#ifndef UART_RX_PIN_CHANGED_CALLBACK_NAME
#define UART_RX_PIN_CHANGED_CALLBACK_NAME uart_rx_pin_changed
#endif

#ifndef BYTE_RECEIVED_CALLBACK_NAME
#define BYTE_RECEIVED_CALLBACK_NAME byte_received
#endif

#ifndef SEND_BYTE_FUNC_NAME
#define SEND_BYTE_FUNC_NAME send_byte
#endif

#define PACKED __attribute__((packed))

typedef enum PACKED {
   RX_WAITING_FOR_START,
   RX_CONFIRMING_START,

   RX_LAST_DATA_BIT_READ_READING_STOP = RX_CONFIRMING_START + 9
} rx_state_t;

typedef enum PACKED {
   TX_IDLE,
   TX_LAST_DATA_BIT_SENT = TX_IDLE + 9,
   TX_SENDING_STOP

} tx_state_t;

typedef struct {
   unsigned char buffer;

   unsigned char counter;
   // unsigned char left_data_bits;   

} uart_common_state_t;

static struct {
   uart_common_state_t common;

   rx_state_t state;

} uart_receiving;

static struct {
   uart_common_state_t common;

   tx_state_t state;

} uart_sending;


static inline void uart_reset() {
   uart_receiving.state = RX_WAITING_FOR_START;
   uart_sending.state   = TX_IDLE;
   
   START_LISTEN_RX_CHANGE;
   STOP_UART_TIMER;

   TX_UP;
} 

static inline void start_uart_timer() {
   /* while uart timer working, pin change interrupt will not be used to
      detect start condition, so it will be disabled */

   STOP_LISTEN_RX_CHANGE;

   START_UART_TIMER;
}

static inline void stop_uart_timer() {
   START_LISTEN_RX_CHANGE;

   STOP_UART_TIMER;
}

static inline void setup_receiving() {
   uart_receiving.state          = RX_CONFIRMING_START;
   uart_receiving.common.counter = 0;
}

static inline void rx_sample_bit() {
   uart_receiving.common.buffer >>= 1;

   if (IS_BIT_ON_RX_SET) {
      uart_receiving.common.buffer |= 0x80;
   } else {
      uart_receiving.common.buffer &= ~0x80;
   }
}

static inline void UART_RX_PIN_CHANGED_CALLBACK_NAME() {
   if (!IS_BIT_ON_RX_SET) {
      setup_receiving();
      start_uart_timer();
   } 
}

static inline void SEND_BYTE_FUNC_NAME(unsigned char byte) {
   if (uart_sending.state != TX_IDLE) return;

   // start sending start bit
   TX_DOWN;

   ++uart_sending.state;

   uart_sending.common.buffer = byte;
   uart_sending.common.counter = 0;

   if (uart_receiving.state == RX_WAITING_FOR_START) {
      // if receiving was in idle too, this means that timer is stopped, start timer
      start_uart_timer();
   } 
}

static inline void UART_TIMER_CALLBACK_NAME() {

   if (uart_receiving.state == RX_WAITING_FOR_START) {
      if (!IS_BIT_ON_RX_SET) {
         setup_receiving();
      } 
   } else {
      ++ uart_receiving.common.counter;

      if (uart_receiving.state == RX_CONFIRMING_START) {

         if (IS_BIT_ON_RX_SET) {
            uart_receiving.state = RX_WAITING_FOR_START;
         } else if (uart_receiving.common.counter == 4) {
            ++uart_receiving.state;

            uart_receiving.common.counter = 0;            
         } 
      } else if (uart_receiving.state == RX_LAST_DATA_BIT_READ_READING_STOP) {
         if (uart_receiving.common.counter == 8) {
            uart_receiving.state = RX_WAITING_FOR_START;
         } 
      } else {
         if (uart_receiving.common.counter == 8) {
            uart_receiving.common.counter = 0;

            ++uart_receiving.state;

            rx_sample_bit();

            if (uart_receiving.state == RX_LAST_DATA_BIT_READ_READING_STOP) {
               byte_received(uart_receiving.common.buffer);
            } 
         } 
      }
   }  

   if (uart_sending.state != TX_IDLE) {
      ++uart_sending.common.counter;

      if (uart_sending.common.counter == 8) {
         uart_sending.common.counter = 0;

         if (uart_sending.state == TX_LAST_DATA_BIT_SENT) {
            TX_UP;
            uart_sending.state = TX_SENDING_STOP;

         } else if (uart_sending.state == TX_SENDING_STOP) {
            uart_sending.state = TX_IDLE;
            
            BYTE_SENDING_DONE_CALLBACK();
         } else {
            if (uart_sending.common.buffer & 1) TX_UP;
            else                                TX_DOWN;

            uart_sending.common.buffer >>= 1;
            ++uart_sending.state;
         } 
      }
   }

   const unsigned char idle =
      uart_receiving.state == RX_WAITING_FOR_START
      && uart_sending.state == TX_IDLE;

   if (idle) {
      stop_uart_timer();
   }
}
