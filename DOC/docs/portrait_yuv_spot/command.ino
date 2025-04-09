/*
 * Simple serial command interface
 * 
 * Commands:
 *   D : Dump screen to serial
 *   R<n> : Restore default config
 *   S / S<n>=<v> : Get/Set Shutter param
 *   I / I<n>=<v> : Get/Set Image param
 *   T / T<n>=<v> : Get/Set TPD param
 */

//
// Command processor state
//
#define CMD_ST_IDLE 0
#define CMD_ST_CMD 1
#define CMD_ST_VAL 2

#define TERM_CHAR 0x0D


//
// Command processor variables
//
static int cmd_state = CMD_ST_IDLE;
static char cmd_op;
static uint16_t cmd_arg;
static uint32_t cmd_val;
static bool cmd_has_val;



// State-machine based character parser
void process_rx_data(char c)
{
  int v;
  
  switch (cmd_state) {
    case CMD_ST_IDLE:
      if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'))) {
        cmd_op = c;
        cmd_has_val = false;
        cmd_arg = 0;
        cmd_state = CMD_ST_CMD;
      }
      break;
    
    case CMD_ST_CMD:
      if (c == TERM_CHAR) {
        process_command();
        cmd_state = CMD_ST_IDLE;
      } else {
        if ((v = is_valid_dec(c)) >= 0) {
          cmd_arg = (cmd_arg * 10) + v;
        } else if (c == '=') {
          cmd_state = CMD_ST_VAL;
          cmd_val = 0;
          cmd_has_val = true;
        } else if (c != ' ') {
          cmd_state = CMD_ST_IDLE;
          Serial.println("Illegal command");
        }
      }
      break;
    
    case CMD_ST_VAL:
      if (c == TERM_CHAR) {
        process_command();
        cmd_state = CMD_ST_IDLE;
      } else {
        if ((v = is_valid_dec(c)) >= 0) {
          cmd_val = (cmd_val * 10) + v;
        } else if (c != ' ') {
          cmd_state = CMD_ST_IDLE;
          Serial.println("Illegal command");
        }
      }
      break;

    default:
      cmd_state = CMD_ST_IDLE;
  }
}


void process_command()
{
  uint8_t reg8;
  uint16_t reg16;
  uint32_t reg32;
  int32_t val;
  ir_error_t err;
  
  switch (cmd_op) {
    case 'd':
    case 'D':
      printImage = true;
      break;
    
    case 'r':
    case 'R':
      enum prop_default_cfg r_cfg;
      switch (cmd_arg) {
        case 0: r_cfg = DEF_CFG_ALL; break;
        case 2: r_cfg = DEF_CFG_TPD; break;
        case 3: r_cfg = DEF_CFG_PROP_PAGE; break;
        case 4: r_cfg = DEF_CFG_USER_CFG; break;
        default: r_cfg = DEF_CFG_ALL;
      }
      err = restore_default_cfg(r_cfg);
      Serial.printf("restore_default_cfg returned %d\n", (int) err);
      break;

    case 's':
    case 'S':
      enum prop_auto_shutter_params s_cfg;
      switch (cmd_arg) {
        case 0: s_cfg = SHUTTER_PROP_SWITCH; break;
        case 1: s_cfg = SHUTTER_PROP_MIN_INTERVAL; break;
        case 2: s_cfg = SHUTTER_PROP_MAX_INTERVAL; break;
        case 4: s_cfg = SHUTTER_PROP_TEMP_THRESHOLD_B; break;
        case 5: s_cfg = SHUTTER_PROP_PROTECT_SWITCH; break;
        case 6: s_cfg = SHUTTER_PROP_ANY_INTERVAL; break;
        case 7: s_cfg = SHUTTER_PROTECT_THR_HIGH_GAIN; break;
        case 8: s_cfg = SHUTTER_PROTECT_THR_LOW_GAIN; break;
        case 9: s_cfg = SHUTTER_PREVIEW_START_1ST_DELAY; break;
        case 10: s_cfg = SHUTTER_PREVIEW_START_2ND_DELAY; break;
        case 11: s_cfg = SHUTTER_CHANGE_GAIN_1ST_DELAY; break;
        case 12: s_cfg = SHUTTER_CHANGE_GAIN_2ND_DELAY; break;
        default: s_cfg = SHUTTER_PROP_SWITCH;
      }
      if (cmd_has_val) {
        err = set_prop_auto_shutter_params(s_cfg, (uint16_t) cmd_val);
        Serial.printf("set_prop_auto_shutter_params(%d) returned %d\n", (int) cmd_arg, (int) err);
      } else {
        err = get_prop_auto_shutter_params(s_cfg, &reg16);
        Serial.printf("get_prop_auto_shutter_params(%d) returned %d = %d\n", (int) cmd_arg, (int) err, (int) reg16);
      }
      break;

    case 'i':
    case 'I':
      enum prop_image_params i_cfg;
      switch (cmd_arg) {
        case 0: i_cfg = IMAGE_PROP_LEVEL_TNR; break;
        case 1: i_cfg = IMAGE_PROP_LEVEL_SNR; break;
        case 2: i_cfg = IMAGE_PROP_LEVEL_DDE; break;
        case 3: i_cfg = IMAGE_PROP_LEVEL_BRIGHTNESS; break;
        case 4: i_cfg = IMAGE_PROP_LEVEL_CONTRAST; break;
        case 5: i_cfg = IMAGE_PROP_MODE_AGC; break;
        case 6: i_cfg = IMAGE_PROP_LEVEL_MAX_GAIN; break;
        case 7: i_cfg = IMAGE_PROP_LEVEL_BOS; break;
        case 8: i_cfg = IMAGE_PROP_ONOFF_AGC; break;
        case 9: i_cfg = IMAGE_PROP_SEL_MIRROR_FLIP; break;
        default: i_cfg = IMAGE_PROP_LEVEL_TNR;
      }
      if (cmd_has_val) {
        err = set_prop_image_params(i_cfg, (uint16_t) cmd_val);
        Serial.printf("set_prop_image_params(%d) returned %d\n", (int) cmd_arg, (int) err);
      } else {
        err = get_prop_image_params(i_cfg, &reg16);
        Serial.printf("get_prop_image_params(%d) returned %d = %d\n", (int) cmd_arg, (int) err, (int) reg16);
      }
      break;

    case 't':
    case 'T':
      enum prop_tpd_params t_cfg;
      switch (cmd_arg) {
        case 0: t_cfg = TPD_PROP_DISTANCE; break;
        case 1: t_cfg = TPD_PROP_TU; break;
        case 2: t_cfg = TPD_PROP_TA; break;
        case 3: t_cfg = TPD_PROP_EMS; break;
        case 4: t_cfg = TPD_PROP_TAU; break;
        case 5: t_cfg = TPD_PROP_GAIN_SEL; break;
        case 6: t_cfg = TPD_PROP_DIST_COR_FACT; break;
        case 7: t_cfg = TPD_PROP_ENV_CORRECT_SEL; break;
        case 8: t_cfg = TPD_PROP_DIST_CORRECT_SEL; break;
        case 9: t_cfg = TPD_PROP_TEMP_MAP_SEL; break;
        default: t_cfg = TPD_PROP_DISTANCE;
      }
      if (cmd_has_val) {
        err = set_prop_tpd_params(t_cfg, (uint16_t) cmd_val);
        Serial.printf("set_prop_tpd_params(%d) returned %d\n", (int) cmd_arg, (int) err);
      } else {
        err = get_prop_tpd_params(t_cfg, &reg16);
        Serial.printf("get_prop_tpd_params(%d) returned %d = %d\n", (int) cmd_arg, (int) err, (int) reg16);
      }
      break;
      
    default:
      Serial.printf("Unknonw command op: %c\n", cmd_op);
  }
}


// Returns -1 for invalid decimal character, 0 - 9 for valid decimal number
int is_valid_dec(char c) {
  if ((c >= '0') && (c <= '9')) {
    return (c - '0');
  }
  return -1;
}
