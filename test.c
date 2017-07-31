#include <stdio.h>
#include <stdlib.h>

static int is_rx_set;
static int is_tx_set;
static int timer_started;
static int listening_rx_pin;

#define START_UART_TIMER (timer_started = 1)
#define STOP_UART_TIMER (timer_started = 0)

#define START_LISTEN_RX_CHANGE (listening_rx_pin = 1)
#define STOP_LISTEN_RX_CHANGE  (listening_rx_pin = 0)

static void error(const char *);

#define NO_BYTE (-1)
#define MESSAGE_LIMIT 1000

static int expected_byte = NO_BYTE;
static int received_byte = NO_BYTE;

static void byte_received(unsigned char byte) {
   received_byte = byte;

   if (expected_byte == NO_BYTE) {
      error("unexpected byte received");
   } else if (expected_byte != byte) {

      char message[MESSAGE_LIMIT];
      
      snprintf(message, MESSAGE_LIMIT, "unexpected byte received: %d, expected: %d", (int)byte, expected_byte);

      error(message);
   } 
   
}

static int rx_sampled_count;

int get_rx_set() {
   ++rx_sampled_count;
   return is_rx_set;
}

#define IS_BIT_ON_RX_SET   get_rx_set()

#define TX_UP (is_tx_set = 1)
#define TX_DOWN (is_tx_set = 0)

void (*sent_action)();

void uart_byte_sent() {
   if (sent_action) sent_action();
}

#include "uart-inline.h"

unsigned char buffer[2];

unsigned int sending_byte;
unsigned int pending_bytes;
unsigned int bytes_sent_callback_called_times;

void start_test(const char *message) {
   rx_sampled_count = 0;
   is_rx_set = 1;
   is_tx_set = 0;
   timer_started = 0;
   listening_rx_pin = 0;

   bytes_sent_callback_called_times = 0;
   sending_byte = 0;
   pending_bytes = 0;

   expected_byte = NO_BYTE;
   received_byte = NO_BYTE;

   puts(message);

   uart_reset();
} 


void assert(const char *assertion, const int condition) {
   if (!condition) {
      char message[MESSAGE_LIMIT];
      
      snprintf(message, MESSAGE_LIMIT, "%s assertion failed", assertion);

      error(message);
   } 
}


void assertTxUp() {
   assert("tx up", 
          is_tx_set);
} 

void assertTxDown() {
   assert("tx down", 
          !is_tx_set);
} 

void assertTxIs(const int expected) {
   if (expected) assertTxUp();
   else          assertTxDown();
} 

void assertTimerStarted() {
   assert("timer started",     
          timer_started);
} 

void assertTimerStopped() {
   assert("timer stopped", 
          !timer_started);
} 

void assertListeningStarted() {
   assert("rx listening started",     
          listening_rx_pin);
} 

void assertListeningStopped() {
   assert("rx listening stopped", 
          !listening_rx_pin);
} 


void test_receive_bit(const int bit) {
   is_rx_set = bit;

   rx_sampled_count = 0;

   for (int i = 0; i < 7; ++i) UART_TIMER_CALLBACK_NAME();

   assert("receiving bit unexpected rx sample", rx_sampled_count == 0);

   UART_TIMER_CALLBACK_NAME();

   assert("receiving bit expected rx sample wasn't performed", rx_sampled_count == 1);
} 

void test_simple_receive(unsigned char byte_transferred) {
   char message[MESSAGE_LIMIT];
   snprintf(message, MESSAGE_LIMIT, "testing simple receive of byte: %x", byte_transferred);
   start_test(message);

   /* imitate dropping of RX line down */
   is_rx_set = 0;
   UART_RX_PIN_CHANGED_CALLBACK_NAME();

   assert("rx sampled by pin change callback", rx_sampled_count == 1);

   assertListeningStopped();
   assertTimerStarted();

   /**
    * as we started from down callback, we no need to sample rx again.  to
    * get into middle of start bit (to confirm start) we will need to do 4
    * more checks (as bit is divided to 8 Parts):
    *
    *  ,- here UART_TIMER_CALLBACK_NAME() executed and started timer
    *  /
    *  |    |    |    |    |    |    |    |    |
    *       \    \    \    \
    *        `----`----`----`--- here timer checks are expected
    *
    *  then we are expecting no RX sampling until middle of first data bit,
    *  which is 12 ticks after RX line down
    */
   
   /* simulate 4 tics */

   rx_sampled_count = 0;
   for (int i = 0; i < 4; ++i) UART_TIMER_CALLBACK_NAME();

   /* verify that rx sampled 4 times */
   
   assert("count rx sampled", rx_sampled_count == 4);

   int byte_buffer = byte_transferred;

   expected_byte = byte_transferred;

   for (int i = 0; i < 8; ++i) {
      test_receive_bit(byte_buffer & 1);
      byte_buffer >>= 1;
   } 

   assert("byte correct", received_byte == expected_byte);
}

void test_sending(unsigned char byte_transferred) {
   char message[MESSAGE_LIMIT];
   snprintf(message, MESSAGE_LIMIT, "testing simple send of byte: %x", byte_transferred);
   start_test(message);

   assertTimerStopped();
   assertListeningStarted();
   assertTxUp();

   SEND_BYTE_FUNC_NAME(byte_transferred);

   assertTimerStarted();
   assertListeningStopped();

   assertTxDown();

   /* sending start bit */
   for (int i = 0; i < 7; ++i) {
      UART_TIMER_CALLBACK_NAME();

      assertTxDown();
   } 

   for (int i = 0; i < 8; ++i) {
      int expectedState = byte_transferred & 1;
      byte_transferred >>= 1;

      /* 8 ticks to next state */
      for (int b = 0; b < 8; ++b){
         UART_TIMER_CALLBACK_NAME();

         assertTxIs(expectedState);
      } 
   }

   /* eight bytes up, stop bit */

   for (int i = 0; i < 8; ++i) {
      UART_TIMER_CALLBACK_NAME();
      assertTxUp();

      assertTimerStarted();
   } 

   UART_TIMER_CALLBACK_NAME();
   
   assertTimerStopped();
   assertListeningStarted();
} 


void send_next() {
   bytes_sent_callback_called_times++;

   if (sending_byte < pending_bytes) {
      
      SEND_BYTE_FUNC_NAME(buffer[sending_byte]);

      sending_byte++;
   } 
} 

void test_sending_interleaved_with_receiving(unsigned char first_send_byte, 
                                             unsigned char second_send_byte,
                                             unsigned char receive_byte) {
   start_test("testing sending interleaved with receiving");

   buffer[0] = first_send_byte;
   buffer[1] = second_send_byte;
   sending_byte  = 0;
   pending_bytes = 2;
   
   assertTimerStopped();
   assertListeningStarted();
   assertTxUp();

   sent_action = &send_next;

   send_next();

   assertTimerStarted();
   assertListeningStopped();
   assertTxDown();


   for (int i = 0; i < 16; ++i) UART_TIMER_CALLBACK_NAME();

   /* and start receiving by lowering the rx line */
   is_rx_set = 0;

   /* and continue sending the start bit */
   for (int i = 0; i < 8; ++i) UART_TIMER_CALLBACK_NAME();

   expected_byte = receive_byte;

   for (int i = 0; i < 8; ++i) {

      is_rx_set = receive_byte & 1;
      receive_byte >>= 1;

      for (int b = 0; b < 8; ++b) UART_TIMER_CALLBACK_NAME();
   } 

   assert("correct byte received", expected_byte == received_byte);
   
   /* last ticks to make it complete sending */
   for (int i = 0; i < 71; ++i) {
      UART_TIMER_CALLBACK_NAME();

      assertTimerStarted();
      assertListeningStopped();
   } 


   UART_TIMER_CALLBACK_NAME();

   assertTimerStopped();
   assertListeningStarted();

   assert("correct count of callback called times", 
          bytes_sent_callback_called_times == 3);
} 

int main() {

   /* ensure every byte received correctly */
   for (int i = 0; i < 0x100; ++i) {
      test_simple_receive(i);      
      test_sending(i);
   } 

   test_sending_interleaved_with_receiving(0x35, 0xf1, 0xd4);

   return 0;
} 


void error(const char *data) {
   puts(data);
   exit(1);
} 
