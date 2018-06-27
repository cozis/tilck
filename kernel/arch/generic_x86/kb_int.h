
#define KB_ITERS_TIMEOUT (1000*1000)

#define KB_DATA_PORT     0x60
#define KB_CONTROL_PORT  0x64

/* keyboard interface bits */

/* ctrl bit 0: must be set before attempting to read data from data port */
#define KB_CTRL_OUTPUT_FULL       (1 << 0)

/* ctrl bit 1: must be clear before attempting to write data to the data or control port */
#define KB_CTRL_INPUT_FULL        (1 << 1)

#define KB_CMD_CPU_RESET             0xFE
#define KB_CTRL_CMD_RESET            0xFF
#define KB_CTRL_CMD_SELFTEST         0xAA

#define KB_CTRL_CMD_PORT1_DISABLE    0xAD
#define KB_CTRL_CMD_PORT1_ENABLE     0xAE
#define KB_CTRL_CMD_PORT2_DISABLE    0xA7
#define KB_CTRL_CMD_PORT2_ENABLE     0xA8


#define KB_RESPONSE_ACK              0xFA
#define KB_RESPONSE_RESEND           0xFE
#define KB_RESPONSE_BAT_OK           0xAA
#define KB_RESPONSE_SELF_TEST_OK     0x55

static ALWAYS_INLINE NODISCARD bool kb_ctrl_is_pending_data(void)
{
   return !!(inb(KB_CONTROL_PORT) & KB_CTRL_OUTPUT_FULL);
}

static ALWAYS_INLINE NODISCARD bool kb_ctrl_is_read_for_next_cmd(void)
{
   return !(inb(KB_CONTROL_PORT) & KB_CTRL_INPUT_FULL);
}

static ALWAYS_INLINE NODISCARD bool kb_wait_cmd_fetched(void)
{
   for (int i = 0; !kb_ctrl_is_read_for_next_cmd(); i++) {

      if (i >= KB_ITERS_TIMEOUT)
         return false;
   }

   return true;
}

static ALWAYS_INLINE NODISCARD bool kb_wait_for_data(void)
{
   for (int i = 0; !kb_ctrl_is_pending_data(); i++) {

      if (i >= KB_ITERS_TIMEOUT)
         return false;
   }

   return true;
}

static ALWAYS_INLINE void kb_drain_any_data(void)
{
   while (kb_ctrl_is_pending_data()) {
      inb(KB_DATA_PORT);
   }
}

static NODISCARD bool kb_ctrl_send_cmd(u8 cmd)
{
   if (!kb_wait_cmd_fetched())
      return false;

   outb(KB_CONTROL_PORT, cmd);

   if (!kb_wait_cmd_fetched())
      return false;

   return true;
}

static NODISCARD bool kb_ctrl_send_cmd_and_wait_response(u8 cmd)
{
   if (!kb_ctrl_send_cmd(cmd))
      return false;

   if (!kb_wait_for_data())
      return false;

   return true;
}

static void kb_ctrl_disable_ports(void)
{
   if (!kb_ctrl_send_cmd(KB_CTRL_CMD_PORT1_DISABLE))
      panic("KB: send cmd timed out");

   if (!kb_ctrl_send_cmd(KB_CTRL_CMD_PORT2_DISABLE))
      panic("KB: send cmd timed out");
}

static void kb_ctrl_enable_ports(void)
{
   if (!kb_ctrl_send_cmd(KB_CTRL_CMD_PORT1_ENABLE))
      panic("KB: send cmd timed out");

   if (!kb_ctrl_send_cmd(KB_CTRL_CMD_PORT2_ENABLE))
      panic("KB: send cmd timed out");
}

static void kb_ctrl_full_wait(void)
{
   u8 temp;

   /* Clear all keyboard buffers (output and command buffers) */
   do
   {
      temp = inb(KB_CONTROL_PORT);

      if (temp & KB_CTRL_OUTPUT_FULL) {
         inb(KB_DATA_PORT);
      }

   } while (temp & KB_CTRL_INPUT_FULL);
}

static NODISCARD bool kb_ctrl_self_test(void)
{
   u8 res, resend_count = 0;
   bool success = false;

   do {

      if (resend_count >= 3)
         break;

      if (!kb_ctrl_send_cmd_and_wait_response(KB_CTRL_CMD_SELFTEST))
         goto out;

      res = inb(KB_DATA_PORT);
      resend_count++;

   } while (res == KB_RESPONSE_RESEND);

   if (res == KB_RESPONSE_SELF_TEST_OK)
      success = true;

out:
   return success;
}

static NODISCARD bool kb_ctrl_reset(void)
{
   u8 res;
   u8 kb_ctrl;
   u8 resend_count = 0;
   bool success = false;

   kb_ctrl = inb(KB_CONTROL_PORT);

   printk("KB: reset procedure\n");
   printk("KB: initial status: 0x%x\n", kb_ctrl);
   printk("KB: sending 0xFF (reset) to KB (data)\n");

   if (!kb_ctrl_send_cmd_and_wait_response(KB_CTRL_CMD_RESET))
      goto out;

   do {

      if (resend_count >= 3)
         break;

      res = inb(KB_DATA_PORT);
      printk("KB: response: 0x%x\n", res);
      resend_count++;

   } while (res == KB_RESPONSE_RESEND);

   if (res != KB_RESPONSE_ACK) {

      if (res == KB_RESPONSE_BAT_OK)
         success = true;

      goto out;
   }

   /* We got an ACK, now wait for the success/failure of the reset itself */

   if (!kb_wait_for_data())
      goto out;

   res = inb(KB_DATA_PORT);
   printk("KB: response: 0x%x\n", res);

   if (res == KB_RESPONSE_BAT_OK)
      success = true;

out:
   return success;
}
