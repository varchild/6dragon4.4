/***************************************************************************
 * - Chronicles Copyright 2001, 2002 by Brad Ensley (Orion Elder)          *
 * - SMAUG 1.4  Copyright 1994, 1995, 1996, 1998 by Derek Snider           *
 * - Merc  2.1  Copyright 1992, 1993 by Michael Chastain, Michael Quan,    *
 *   and Mitchell Tse.                                                     *
 * - DikuMud    Copyright 1990, 1991 by Sebastian Hammer, Michael Seifert, *
 *   Hans-Henrik St�rfeldt, Tom Madsen, and Katja Nyboe.                   *
 ***************************************************************************
 * - Command interpretation module                                         *
 ***************************************************************************/

#include <ctype.h>
#include <string.h>
#include <time.h>
#include "h/mud.h"
#include "h/files.h"
#include "h/clans.h"
/*
 * Externals
 */
void                    refresh_page(CHAR_DATA *ch);
void                    subtract_times(struct timeval *etime, struct timeval *stime);
bool                    local_channel_hook(CHAR_DATA *ch, const char *command, char *argument);
bool check_social       args((CHAR_DATA *ch, char *command, char *argument));
char                   *check_cmd_flags args((CHAR_DATA *ch, CMDTYPE * cmd));
extern CHAR_DATA       *timechar;

/*
 * Log-all switch.
 */
bool                    fLogAll = FALSE;

CMDTYPE                *command_hash[126];  /* hash table for cmd_table */
SOCIALTYPE             *social_index[27]; /* hash table for socials */
BANK_DATA              *bank_index[27]; /* hash table for bank */

/*
 * Character not in position for command?
 */
bool check_pos(CHAR_DATA *ch, short position)
{

  if(IS_NPC(ch) && ch->position > 3)  /* Band-aid alert? -- Blod */
    return TRUE;

  if(!IS_NPC(ch) && ch->position == POS_CRAWL)
    return TRUE;

  if(ch->position < position)
  {
    switch (ch->position)
    {
      default:
        bug("check_pos - add position %d", position);
        break;

      case POS_DEAD:
        send_to_char("A little difficult to do when you are DEAD...\r\n", ch);
        break;

      case POS_MORTAL:
      case POS_INCAP:
        send_to_char("You are hurt far too bad for that.\r\n", ch);
        send_to_char("You may choose to expire to end your dying state...\r\n", ch);
        send_to_char("type 'help expire'\r\n", ch);
        break;

      case POS_STUNNED:
        if(!IS_AFFECTED(ch, AFF_FEIGN) || !IS_AFFECTED(ch, AFF_PARALYSIS))
        {
          send_to_char("You are too stunned to do that.\r\n", ch);
          break;
        }
        else
        {
          return TRUE;
        }

      case POS_SLEEPING:
        if(IS_AFFECTED(ch, AFF_FEIGN))
        {
          send_to_char("You must stop feigning death and stand up first.\r\n", ch);
          break;
        }
        else
        {
          send_to_char("In your dreams, or what?\r\n", ch);
        }
        break;

      case POS_MEDITATING:
        send_to_char("You are concentrating too hard for that.\r\n", ch);
        break;

      case POS_RESTING:
        send_to_char("Nah... You feel too relaxed...\r\n", ch);
        break;

      case POS_SITTING:
        send_to_char("You can't do that sitting down.\r\n", ch);
        break;

      case POS_FIGHTING:
        if(position <= POS_EVASIVE)
        {
          send_to_char("This fighting style is too demanding for that!\r\n", ch);
        }
        else
        {
          send_to_char("No way!  You are still fighting!\r\n", ch);
        }
        break;
      case POS_DEFENSIVE:
        if(position <= POS_EVASIVE)
        {
          send_to_char("This fighting style is too demanding for that!\r\n", ch);
        }
        else
        {
          send_to_char("No way!  You are still fighting!\r\n", ch);
        }
        break;
      case POS_AGGRESSIVE:
        if(position <= POS_EVASIVE)
        {
          send_to_char("This fighting style is too demanding for that!\r\n", ch);
        }
        else
        {
          send_to_char("No way!  You are still fighting!\r\n", ch);
        }
        break;
      case POS_BERSERK:
        if(position <= POS_EVASIVE)
        {
          send_to_char("This fighting style is too demanding for that!\r\n", ch);
        }
        else
        {
          send_to_char("No way!  You are still fighting!\r\n", ch);
        }
        break;
      case POS_EVASIVE:
        send_to_char("No way!  You are still fighting!\r\n", ch);
        break;

    }
    return FALSE;
  }
  return TRUE;
}

extern char             lastplayercmd[MIL * 2];

/*
 * The main entry point for executing commands.
 * Can be recursively called from 'at', 'order', 'force'.
 */
void interpret(CHAR_DATA *ch, char *argument)
{
  char                    command[MIL];
  char                    logline[MIL];
  char                    logname[MIL];
  char                   *buf;
  TIMER                  *timer = NULL;
  CMDTYPE                *cmd = NULL;
  int                     trust;
  int                     loglvl;
  bool                    found;
  struct timeval          time_used;
  long                    tmptime;
  char                   *autofull;

  if(!ch)
  {
    bug("%s", "interpret: null ch!");
    return;
  }

  if(!ch->in_room)
  {
    bug("%s", "interpret: null in_room!");
    return;
  }
  found = FALSE;
  if(ch->substate == SUB_REPEATCMD)
  {
    DO_FUN                 *fun;

    if((fun = ch->last_cmd) == NULL)
    {
      ch->substate = SUB_NONE;
      bug("%s", "interpret: SUB_REPEATCMD with NULL last_cmd");
      return;
    }
    else
    {
      int                     x;

      /*
       * yes... we lose out on the hashing speediness here...
       * but the only REPEATCMDS are wizcommands (currently)
       */
      for(x = 0; x < 126; x++)
      {
        for(cmd = command_hash[x]; cmd; cmd = cmd->next)
          if(cmd->do_fun == fun)
          {
            found = TRUE;
            break;
          }
        if(found)
          break;
      }
      if(!found)
      {
        cmd = NULL;
        bug("%s", "interpret: SUB_REPEATCMD: last_cmd invalid");
        return;
      }
    }
  }

  if(!cmd)
  {
    /*
     * Changed the order of these ifchecks to prevent crashing. 
     */
    if(!argument || !strcmp(argument, ""))
    {
      bug("%s", "interpret: null argument!");
      return;
    }

    /*
     * Strip leading spaces.
     */
    while(isspace(*argument))
      argument++;
    if(argument[0] == '\0')
      return;

    /*
     * xREMOVE_BIT(ch->affected_by, AFF_HIDE); 
     */

    /*
     * Implement freeze command.
     */
    if(!IS_NPC(ch) && xIS_SET(ch->act, PLR_FREEZE))
    {
      send_to_char("You're totally frozen!\r\n", ch);
      return;
    }

    /*
     * Grab the command word.
     * Special parsing so ' can be a command,
     *   also no spaces needed after punctuation.
     */

    /*
     * Volk: dumping argument to use it later! 
     */
    autofull = argument;
    strcpy(logline, argument);
    if(!isalpha(argument[0]) && !isdigit(argument[0]))
    {
      command[0] = argument[0];
      command[1] = '\0';
      argument++;
      while(isspace(*argument))
        argument++;
    }
    else
      argument = one_argument(argument, command);

    /*
     * Look for command in command table.
     * Check for council powers and/or bestowments
     */
    trust = get_trust(ch);

    for(cmd = command_hash[LOWER(command[0]) % 126]; cmd; cmd = cmd->next)
      if(((!IS_SET(cmd->flags, CMD_FULLNAME) && !str_prefix(command, cmd->name))
          || (IS_SET(cmd->flags, CMD_FULLNAME) && !str_cmp(command, cmd->name)))
         && (cmd->level <= trust
             || (!IS_NPC(ch) && ch->pcdata->council
                 && is_name(cmd->name, ch->pcdata->council->powers)
                 && cmd->level <= (trust + MAX_CPD)) || (!IS_NPC(ch) && VLD_STR(ch->pcdata->bestowments) && is_name(cmd->name, ch->pcdata->bestowments) && cmd->level <= (trust + sysdata.bestow_dif))))
      {
        found = TRUE;
        break;
      }

    if(!IS_NPC(ch) && IS_SET(ch->pcdata->flags, PCFLAG_AIDLE))
    {
      REMOVE_BIT(ch->pcdata->flags, PCFLAG_AIDLE);
      act(AT_GREY, "$n is no longer idle.", ch, NULL, NULL, TO_CANSEE);
    }
  }


  /*
   * Turn off afk bit when any command performed. 
   */
  if(!IS_NPC(ch) && xIS_SET(ch->act, PLR_AFK) && (str_cmp(command, "AFK")))
  {
    xREMOVE_BIT(ch->act, PLR_AFK);
    act(AT_GREY, "$n is no longer afk.", ch, NULL, NULL, TO_CANSEE);
#ifdef I3
    if(I3IS_SET(I3FLAG(ch), I3_AFK))
    {
      send_to_char("You're no longer AFK to I3.\r\n", ch);
      I3REMOVE_BIT(I3FLAG(ch), I3_AFK);
    }
#endif
  }

  /*
   * Log and snoop.
   */
  snprintf(lastplayercmd, MIL, "%s used %s", ch->name, logline);

  if(found && cmd->log == LOG_NEVER)
    strcpy(logline, "XXXXXXXX XXXXXXXX XXXXXXXX");

  loglvl = found ? cmd->log : LOG_NORMAL;

  if((!IS_NPC(ch) && xIS_SET(ch->act, PLR_LOG)) || fLogAll || loglvl == LOG_BUILD || loglvl == LOG_HIGH || loglvl == LOG_ALWAYS)
  {
    /*
     * Added by Narn to show who is switched into a mob that executes
     * a logged command.  Check for descriptor in case force is used. 
     */

    if(ch->desc && ch->desc->original)
      snprintf(log_buf, MAX_STRING_LENGTH, "Log %s (%s): %s", ch->name, ch->desc->original->name, logline);
    else
      snprintf(log_buf, MAX_STRING_LENGTH, "Log %s: %s", ch->name, logline);

    /*
     * Make it so a 'log all' will send most output to the log
     * file only, and not spam the log channel to death   -Thoric
     */
    if(fLogAll && loglvl == LOG_NORMAL && (IS_NPC(ch) || !xIS_SET(ch->act, PLR_LOG)))
      loglvl = LOG_ALL;

    log_string_plus(log_buf, loglvl, get_trust(ch));
  }

  if(ch->desc && ch->desc->snoop_by)
  {
    snprintf(logname, MIL, "%s", ch->name);
    write_to_buffer(ch->desc->snoop_by, logname, 0);
    write_to_buffer(ch->desc->snoop_by, "% ", 2);
    write_to_buffer(ch->desc->snoop_by, logline, 0);
    write_to_buffer(ch->desc->snoop_by, "\r\n", 2);
  }

  /*
   * check for a timer delayed command (search, dig, detrap, etc) 
   */
  if((timer = get_timerptr(ch, TIMER_DO_FUN)) != NULL)
  {
    int                     tempsub;

    tempsub = ch->substate;
    ch->substate = SUB_TIMER_DO_ABORT;
    (timer->do_fun) (ch, (char *)"");
    if(char_died(ch))
      return;
    if(ch->substate != SUB_TIMER_CANT_ABORT)
    {
      ch->substate = tempsub;
      extract_timer(ch, timer);
    }
    else
    {
      ch->substate = tempsub;
      return;
    }
  }

  /*
   * Look for command in skill and socials table.
   */
  if(!found)
  {

    if(!str_cmp(command, "hint") && !IS_IMMORTAL(ch))
    {
      send_to_char("Huh?\r\n", ch);
      return;
    }

    if(!local_channel_hook(ch, command, argument) && !check_skill(ch, command, argument) && !check_alias(ch, command, argument) && !check_social(ch, command, argument)
#ifdef I3
       && !i3_command_hook(ch, command, argument)
#endif
      )
    {
      EXIT_DATA              *pexit;

      /*
       * check for an auto-matic exit command 
       */
      if((pexit = find_door(ch, command, TRUE)) != NULL && (IS_SET(pexit->exit_info, EX_xAUTO) || (IS_SET(pexit->exit_info, EX_AUTOFULL) && !str_cmp(pexit->keyword, autofull))))
      {
        if(IS_SET(pexit->exit_info, EX_CLOSED) && ((!IS_AFFECTED(ch, AFF_PASS_DOOR) && !xIS_SET(ch->act, PLR_SHADOWFORM)) || (IS_SET(pexit->exit_info, EX_NOPASSDOOR) || !IS_IMMORTAL(ch))))
        {
          if(!IS_SET(pexit->exit_info, EX_SECRET))
            act(AT_PLAIN, "The $d is closed.", ch, NULL, pexit->keyword, TO_CHAR);
          else
            send_to_char("You cannot do that here.\r\n", ch);
          return;
        }
        if( check_pos( ch, POS_STANDING ) )
        move_char(ch, pexit, 0, FALSE);
        return;
      }
      error(ch);
    }
    return;
  }

  /*
   * Character not in position for command?
   */
  if(!check_pos(ch, cmd->position))
    return;

  /*
   * There is no point in checking the command all the time only check if they are
   * flagged 
   */
  if(xIS_SET(ch->act, PLR_TEASE))
  {
    if(str_cmp(command, "north") && str_cmp(command, "east") && str_cmp(command, "west")
       && str_cmp(command, "south") && str_cmp(command, "forfeit")
       && str_cmp(command, "scan")
       && str_cmp(command, "give") && str_cmp(command, "inv") && str_cmp(command, "inventory") && str_cmp(command, "e") && str_cmp(command, "w") && str_cmp(command, "n") && str_cmp(command, "s"))
    {
      send_to_char("You cannot do that during the Dragon Tease Event.\r\n", ch);
      return;
    }
  }
  /*
   * Berserk check for flee.. maybe add drunk to this?.. but too much
   * hardcoding is annoying.. -- Altrag
   * This wasn't catching wimpy --- Blod
   * if(!str_cmp(cmd->name, "flee") &&
   * IS_AFFECTED(ch, AFF_BERSERK))
   * {
   * send_to_char("You aren't thinking very clearly..\r\n", ch);
   * return;
   * } 
   */

  /*
   * So we can check commands for things like Posses and Polymorph
   * *  But still keep the online editing ability.  -- Shaddai
   * *  Send back the message to print out, so we have the option
   * *  this function might be usefull elsewhere.  Also using the
   * *  send_to_char_color so we can colorize the strings if need be. --Shaddai
   */

  buf = check_cmd_flags(ch, cmd);

  if(buf[0] != '\0')
  {
    send_to_char_color(buf, ch);
    return;
  }

  /*
   * Nuisance stuff -- Shaddai
   */

  if(!IS_NPC(ch) && ch->pcdata->nuisance && ch->pcdata->nuisance->flags > 9 && number_percent() < ((ch->pcdata->nuisance->flags - 9) * 10 * ch->pcdata->nuisance->power))
  {
    send_to_char("You can't seem to do that just now.\r\n", ch);
    return;
  }

  /*
   * Dispatch the command.
   */

  ch->last_cmd = cmd->do_fun;
  start_timer(&time_used);
  (*cmd->do_fun) (ch, argument);
  end_timer(&time_used);
  /*
   * Update the record of how many times this command has been used (haus)
   */
  update_userec(&time_used, &cmd->userec);
  tmptime = UMIN(time_used.tv_sec, 19) * 1000000 + time_used.tv_usec;

  /*
   * laggy command notice: command took longer than 1.5 seconds 
   */
  if(tmptime > 1500000)
  {

#ifdef TIMEFORMAT
    snprintf(buf, MSL, "[*****] LAG: %s: %s %s (R:%d S:%ld.%06ld)", ch->name,
             cmd->name, (cmd->log == LOG_NEVER ? "XXX" : argument), ch->in_room ? ch->in_room->vnum : 0, time_used.tv_sec, time_used.tv_usec);
#else
    snprintf(buf, MSL, "[*****] LAG: %s: %s %s (R:%d S:%ld.%06ld)", ch->name,
             cmd->name, (cmd->log == LOG_NEVER ? "XXX" : argument), ch->in_room ? ch->in_room->vnum : 0, time_used.tv_sec, time_used.tv_usec);
#endif
//        log_string_plus( log_buf, LOG_NORMAL, get_trust( ch ) );
    cmd->lag_count++;
  }

  tail_chain();
}

CMDTYPE                *find_command(const char *command)
{
  CMDTYPE                *cmd;
  int                     hash;

  hash = LOWER(command[0]) % 126;

  for(cmd = command_hash[hash]; cmd; cmd = cmd->next)
    if(!str_prefix(command, cmd->name))
      return cmd;
  return NULL;
}

SOCIALTYPE             *find_social(const char *command)
{
  SOCIALTYPE             *social;
  int                     hash;

  if(command[0] < 'a' || command[0] > 'z')
    hash = 0;
  else
    hash = (command[0] - 'a') + 1;

  for(social = social_index[hash]; social; social = social->next)
    if(!str_prefix(command, social->name))
      return social;

  return NULL;
}

bool check_social(CHAR_DATA *ch, char *command, char *argument)
{
  char                    arg[MIL];
  char                    buf[MIL];
  CHAR_DATA              *victim = NULL;
  SOCIALTYPE             *social;
  CHAR_DATA              *remfirst, *remlast, *remtemp; /* for ignore cmnd */

  if((social = find_social(command)) == NULL)
    return FALSE;

  if(!IS_NPC(ch) && xIS_SET(ch->act, PLR_NO_EMOTE))
  {
    send_to_char("You are anti-social!\r\n", ch);
    return TRUE;
  }

  switch (ch->position)
  {
    case POS_DEAD:
      send_to_char("Lie still; you are DEAD.\r\n", ch);
      return TRUE;

    case POS_INCAP:
    case POS_MORTAL:
      send_to_char("You are hurt far too bad for that.\r\n", ch);
      return TRUE;
    case POS_MEDITATING:
      send_to_char("You are concentrating to much for that.\r\n", ch);
      return TRUE;

    case POS_STUNNED:
      send_to_char("You are too stunned to do that.\r\n", ch);
      return TRUE;

    case POS_SLEEPING:
      /*
       * I just know this is the path to a 12" 'if' statement.  :(
       * But two players asked for it already!  -- Furey
       */
      if(!str_cmp(social->name, "snore"))
        break;
      send_to_char("In your dreams, or what?\r\n", ch);
      return TRUE;

  }

  remfirst = NULL;
  remlast = NULL;
  remtemp = NULL;
  bool                    invobj = FALSE;
  bool                    roomobj = FALSE;
  CHAR_DATA              *rch;

  one_argument(argument, arg);
  if(!arg || arg[0] == '\0')
  {
    if(!social->others_no_arg || !social->char_no_arg)
    {
      send_to_char("Incomplete social, try again soon.\r\n", ch);
      bug("%s: Incomplete Social %s, has NULL others_no_arg or NULL char_no_arg", __FUNCTION__, social->name);
      return TRUE;
    }

    act(AT_SOCIAL, social->others_no_arg, ch, NULL, victim, TO_ROOM);
    act(AT_SOCIAL, social->char_no_arg, ch, NULL, victim, TO_CHAR);

    for(rch = ch->in_room->first_person; rch; rch = rch->next_in_room)
    {
      if(!IS_NPC(ch))
      {
        if(xIS_SET(rch->act, PLR_ENHANCED))
        {
          if(!str_cmp(social->name, "laugh") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalelaugh.wav)\r\n", rch);
          else if(!str_cmp(social->name, "laugh") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malelaugh.wav)\r\n", rch);
          else if(!str_cmp(social->name, "applaud") || !str_cmp(social->name, "clap"))
            send_to_char("!!SOUND(sound/applaud.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cackle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalecackle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cackle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malecackle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "muhaha") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalemuhah.wav)\r\n", rch);
          else if(!str_cmp(social->name, "muhaha") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malemuhah.wav)\r\n", rch);

          else if(!str_cmp(social->name, "giggle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malegiggle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "giggle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalegiggle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "flutter") && ch->sex == 2)
            send_to_char("!!SOUND(sound/flutter.wav)\r\n", rch);
          else if(!str_cmp(social->name, "sing") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalesing.wav)\r\n", rch);
          else if(!str_cmp(social->name, "sing") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malesing.wav)\r\n", rch);
          else if(!str_cmp(social->name, "pfft"))
            send_to_char("!!SOUND(sound/pfft.wav)\r\n", rch);
          else if(!str_cmp(social->name, "snicker") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malesnicker.wav)\r\n", rch);
          else if(!str_cmp(social->name, "snicker") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalesnicker.wav)\r\n", rch);
          else if(!str_cmp(social->name, "chuckle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malechuckle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "chuckle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalechuckle.wav)\r\n", rch);

          else if(!str_cmp(social->name, "yawn"))
            send_to_char("!!SOUND(sound/yawn.wav)\r\n", rch);
          else if(!str_cmp(social->name, "burp"))
            send_to_char("!!SOUND(sound/burp.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cough") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalecough.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cough") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malecough.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cry"))
            send_to_char("!!SOUND(sound/cry.wav)\r\n", rch);
          else if(!str_cmp(social->name, "whistle"))
            send_to_char("!!SOUND(sound/whistle.wav)\r\n", rch);
        }
      }
    }
  }
  else if(!(victim = get_char_room(ch, arg)))
  {
    for(victim = remfirst; victim; victim = victim->next_in_room)
    {
      if(nifty_is_name(victim->name, arg) || nifty_is_name_prefix(arg, victim->name))
      {
        set_char_color(AT_IGNORE, ch);
        ch_printf(ch, "%s is ignoring you.\r\n", victim->name);
        break;
      }
    }
    if(!victim)
      send_to_char("They aren't here.\r\n", ch);
  }
  else if(victim != NULL && !IS_NPC(victim) && victim != ch)
  {
    if(!social->vict_found || !social->char_found || !social->others_found)
    {
      send_to_char("Incomplete social, try again soon.\r\n", ch);
      bug("%s: Incomplete Social %s, NULL vict_found, NULL char_found or NULL others_found", __FUNCTION__, social->name);
      return TRUE;
    }

    for(rch = ch->in_room->first_person; rch; rch = rch->next_in_room)
    {
      if(!IS_NPC(ch))
      {
        if(xIS_SET(rch->act, PLR_ENHANCED))
        {
          if(!str_cmp(social->name, "laugh") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalelaugh.wav)\r\n", rch);
          else if(!str_cmp(social->name, "laugh") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malelaugh.wav)\r\n", rch);
          else if(!str_cmp(social->name, "applaud") || !str_cmp(social->name, "clap"))
            send_to_char("!!SOUND(sound/applaud.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cackle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalecackle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cackle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malecackle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "muhaha") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalemuhah.wav)\r\n", rch);
          else if(!str_cmp(social->name, "muhaha") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malemuhah.wav)\r\n", rch);

          else if(!str_cmp(social->name, "giggle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malegiggle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "giggle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalegiggle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "flutter") && ch->sex == 2)
            send_to_char("!!SOUND(sound/flutter.wav)\r\n", rch);
          else if(!str_cmp(social->name, "sing") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalesing.wav)\r\n", rch);
          else if(!str_cmp(social->name, "sing") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malesing.wav)\r\n", rch);
          else if(!str_cmp(social->name, "pfft"))
            send_to_char("!!SOUND(sound/pfft.wav)\r\n", rch);
          else if(!str_cmp(social->name, "snicker") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malesnicker.wav)\r\n", rch);
          else if(!str_cmp(social->name, "snicker") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalesnicker.wav)\r\n", rch);
          else if(!str_cmp(social->name, "chuckle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malechuckle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "chuckle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalechuckle.wav)\r\n", rch);

          else if(!str_cmp(social->name, "yawn"))
            send_to_char("!!SOUND(sound/yawn.wav)\r\n", rch);
          else if(!str_cmp(social->name, "burp"))
            send_to_char("!!SOUND(sound/burp.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cough") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalecough.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cough") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malecough.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cry"))
            send_to_char("!!SOUND(sound/cry.wav)\r\n", rch);
          else if(!str_cmp(social->name, "whistle"))
            send_to_char("!!SOUND(sound/whistle.wav)\r\n", rch);
        }
      }

    }
    act(AT_SOCIAL, social->vict_found, ch, NULL, victim, TO_VICT);
    act(AT_SOCIAL, social->char_found, ch, NULL, victim, TO_CHAR);
    act(AT_SOCIAL, social->others_found, ch, NULL, victim, TO_NOTVICT);
  }
  else if(victim == ch)
  {
    if(!social->char_auto || !social->others_auto)
    {
      send_to_char("Incomplete social, try again soon.\r\n", ch);
      bug("%s: Incomplete Social %s, NULL char_auto or NULL others_auto", __FUNCTION__, social->name);
      return TRUE;
    }

    for(rch = ch->in_room->first_person; rch; rch = rch->next_in_room)
    {
      if(!IS_NPC(ch))
      {
        if(xIS_SET(rch->act, PLR_ENHANCED))
        {
          if(!str_cmp(social->name, "laugh") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalelaugh.wav)\r\n", rch);
          else if(!str_cmp(social->name, "laugh") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malelaugh.wav)\r\n", rch);
          else if(!str_cmp(social->name, "applaud") || !str_cmp(social->name, "clap"))
            send_to_char("!!SOUND(sound/applaud.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cackle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalecackle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cackle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malecackle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "muhaha") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalemuhah.wav)\r\n", rch);
          else if(!str_cmp(social->name, "muhaha") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malemuhah.wav)\r\n", rch);

          else if(!str_cmp(social->name, "giggle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malegiggle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "giggle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalegiggle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "flutter") && ch->sex == 2)
            send_to_char("!!SOUND(sound/flutter.wav)\r\n", rch);
          else if(!str_cmp(social->name, "sing") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalesing.wav)\r\n", rch);
          else if(!str_cmp(social->name, "sing") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malesing.wav)\r\n", rch);
          else if(!str_cmp(social->name, "pfft"))
            send_to_char("!!SOUND(sound/pfft.wav)\r\n", rch);
          else if(!str_cmp(social->name, "snicker") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malesnicker.wav)\r\n", rch);
          else if(!str_cmp(social->name, "snicker") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalesnicker.wav)\r\n", rch);
          else if(!str_cmp(social->name, "chuckle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malechuckle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "chuckle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalechuckle.wav)\r\n", rch);

          else if(!str_cmp(social->name, "yawn"))
            send_to_char("!!SOUND(sound/yawn.wav)\r\n", rch);
          else if(!str_cmp(social->name, "burp"))
            send_to_char("!!SOUND(sound/burp.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cough") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalecough.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cough") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malecough.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cry"))
            send_to_char("!!SOUND(sound/cry.wav)\r\n", rch);
          else if(!str_cmp(social->name, "whistle"))
            send_to_char("!!SOUND(sound/whistle.wav)\r\n", rch);
        }
      }

    }
    act(AT_SOCIAL, social->others_auto, ch, NULL, victim, TO_ROOM);
    act(AT_SOCIAL, social->char_auto, ch, NULL, victim, TO_CHAR);
  }
  else
  {
    if(!social->char_found || !social->others_found || !social->vict_found)
    {
      send_to_char("Incomplete social, try again soon.\r\n", ch);
      bug("%s: Incomplete Social %s, NULL char_found, NULL others_found or NULL vict_found", __FUNCTION__, social->name);
      return TRUE;
    }

    for(rch = ch->in_room->first_person; rch; rch = rch->next_in_room)
    {
      if(!IS_NPC(ch))
      {
        if(xIS_SET(rch->act, PLR_ENHANCED))
        {
          if(!str_cmp(social->name, "laugh") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalelaugh.wav)\r\n", rch);
          else if(!str_cmp(social->name, "laugh") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malelaugh.wav)\r\n", rch);
          else if(!str_cmp(social->name, "applaud") || !str_cmp(social->name, "clap"))
            send_to_char("!!SOUND(sound/applaud.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cackle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalecackle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cackle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malecackle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "muhaha") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalemuhah.wav)\r\n", rch);
          else if(!str_cmp(social->name, "muhaha") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malemuhah.wav)\r\n", rch);

          else if(!str_cmp(social->name, "giggle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malegiggle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "giggle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalegiggle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "flutter") && ch->sex == 2)
            send_to_char("!!SOUND(sound/flutter.wav)\r\n", rch);
          else if(!str_cmp(social->name, "sing") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalesing.wav)\r\n", rch);
          else if(!str_cmp(social->name, "sing") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malesing.wav)\r\n", rch);
          else if(!str_cmp(social->name, "pfft"))
            send_to_char("!!SOUND(sound/pfft.wav)\r\n", rch);
          else if(!str_cmp(social->name, "snicker") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malesnicker.wav)\r\n", rch);
          else if(!str_cmp(social->name, "snicker") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalesnicker.wav)\r\n", rch);
          else if(!str_cmp(social->name, "chuckle") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malechuckle.wav)\r\n", rch);
          else if(!str_cmp(social->name, "chuckle") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalechuckle.wav)\r\n", rch);

          else if(!str_cmp(social->name, "yawn"))
            send_to_char("!!SOUND(sound/yawn.wav)\r\n", rch);
          else if(!str_cmp(social->name, "burp"))
            send_to_char("!!SOUND(sound/burp.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cough") && ch->sex == 2)
            send_to_char("!!SOUND(sound/femalecough.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cough") && ch->sex == 1)
            send_to_char("!!SOUND(sound/malecough.wav)\r\n", rch);
          else if(!str_cmp(social->name, "cry"))
            send_to_char("!!SOUND(sound/cry.wav)\r\n", rch);
          else if(!str_cmp(social->name, "whistle"))
            send_to_char("!!SOUND(sound/whistle.wav)\r\n", rch);
        }
      }

    }
    act(AT_SOCIAL, social->others_found, ch, NULL, victim, TO_NOTVICT);
    act(AT_SOCIAL, social->char_found, ch, NULL, victim, TO_CHAR);
    act(AT_SOCIAL, social->vict_found, ch, NULL, victim, TO_VICT);

    if(!IS_NPC(ch) && IS_NPC(victim) && !IS_AFFECTED(victim, AFF_CHARM) && IS_AWAKE(victim) && !HAS_PROG(victim->pIndexData, ACT_PROG))
    {
      switch (number_bits(4))
      {
        case 0:
          if(IS_EVIL(ch) && !is_safe(victim, ch, TRUE))

            multi_hit(victim, ch, TYPE_UNDEFINED);
          else if(IS_NEUTRAL(ch))
          {
            act(AT_ACTION, "$n slaps $N.", victim, NULL, ch, TO_NOTVICT);
            act(AT_ACTION, "You slap $N.", victim, NULL, ch, TO_CHAR);
            act(AT_ACTION, "$n slaps you.", victim, NULL, ch, TO_VICT);
          }
          else
          {
            act(AT_ACTION, "$n acts like $N doesn't even exist.", victim, NULL, ch, TO_NOTVICT);
            act(AT_ACTION, "You just ignore $N.", victim, NULL, ch, TO_CHAR);
            act(AT_ACTION, "$n appears to be ignoring you.", victim, NULL, ch, TO_VICT);
          }
          break;

        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
          act(AT_SOCIAL, social->others_found, victim, NULL, ch, TO_NOTVICT);
          act(AT_SOCIAL, social->char_found, victim, NULL, ch, TO_CHAR);
          act(AT_SOCIAL, social->vict_found, victim, NULL, ch, TO_VICT);
          break;

        case 9:
        case 10:
        case 11:
        case 12:
          act(AT_ACTION, "$n slaps $N.", victim, NULL, ch, TO_NOTVICT);
          act(AT_ACTION, "You slap $N.", victim, NULL, ch, TO_CHAR);
          act(AT_ACTION, "$n slaps you.", victim, NULL, ch, TO_VICT);
          break;
      }
    }
  }

  /*
   * Replace the chars in the ignoring list to the room 
   */
  /*
   * note that the ordering of the players in the room  
   */
  /*
   * might change       
   */
  for(victim = remfirst; victim; victim = remtemp)
  {
    remtemp = victim->next_in_room;
    char_to_room(victim, ch->in_room);
  }

  return TRUE;
}

BANK_DATA              *find_bank(char *command)
{
  BANK_DATA              *bank;
  int                     hash;

  /*
   * Volk - attempt fix 
   */
  if(!command || command == "\0")
    return NULL;

  /*
   * Bugged and crashed, why? 
   */
  if(command[0] < 'a' || command[0] > 'z')
    hash = 0;
  else
    hash = (command[0] - 'a') + 1;

  for(bank = bank_index[hash]; bank; bank = bank->next)
    if(!str_prefix(command, bank->name))
      return bank;

  return NULL;
}

/*
 * Return true if an argument is completely numeric.
 */
bool is_number(const char *arg)
{
  bool                    first = TRUE;

  if(*arg == '\0')
    return FALSE;

  for(; *arg != '\0'; arg++)
  {
    if(first && *arg == '-')
    {
      first = FALSE;
      continue;
    }
    if(!isdigit(*arg))
      return FALSE;
    first = FALSE;
  }

  return TRUE;
}

/*
 * Given a string like 14.foo, return 14 and 'foo'
 */
int number_argument(char *argument, char *arg)
{
  char                   *pdot;
  int                     number;

  for(pdot = argument; *pdot != '\0'; pdot++)
  {
    if(*pdot == '.')
    {
      *pdot = '\0';
      number = atoi(argument);
      *pdot = '.';
      strcpy(arg, pdot + 1);
      return number;
    }
  }

  strcpy(arg, argument);
  return 1;
}

/* Like one argument only don't mess with case */
char                   *one_http_argument(char *argument, char *arg_first)
{
  char                    cEnd;
  short                   count;

  count = 0;

  if(!VLD_STR(argument))
  {
    arg_first[0] = '\0';
    return argument;
  }

  while(isspace(*argument))
    argument++;

  cEnd = ' ';
  if(*argument == '\'' || *argument == '"')
    cEnd = *argument++;

  while(*argument != '\0' || ++count >= 255)
  {
    if(*argument == cEnd)
    {
      argument++;
      break;
    }
    *arg_first = *argument;
    arg_first++;
    argument++;
  }
  *arg_first = '\0';

  while(isspace(*argument))
    argument++;

  return argument;
}

/*
 * Pick off one argument from a string and return the rest.
 * Understands quotes. No longer mangles case either. That used to be annoying.
 */
/*
const char             *one_argument(const char *argument, char *arg_first)
{
  char                    cEnd;
  int                     count;

  count = 0;

  while(isspace(*argument))
    argument++;

  cEnd = ' ';
  if(*argument == '\'' || *argument == '"')
    cEnd = *argument++;

  while(*argument != '\0' || ++count >= 255)
  {
    if(*argument == cEnd)
    {
      argument++;
      break;
    }
    *arg_first = (*argument);
    arg_first++;
    argument++;
  }
  *arg_first = '\0';

  while(isspace(*argument))
    argument++;

  return argument;
}
*/

char                   *one_argument(char *argument, char *arg_first)
{
  char                    cEnd;
  short                   count;

  count = 0;

  if(!VLD_STR(argument))
  {
    arg_first[0] = '\0';
    return argument;
  }

  while(isspace(*argument))
    argument++;

  cEnd = ' ';
  if(*argument == '\'' || *argument == '"')
    cEnd = *argument++;

  while(*argument != '\0' || ++count >= 255)
  {
    if(*argument == cEnd)
    {
      argument++;
      break;
    }
/*    if (!arg_first)    Volk - this fix will probably stop crashes, but too tired to consider if it might break something else
      continue;  */
    *arg_first = LOWER(*argument);
    arg_first++;
    argument++;
  }
  *arg_first = '\0';

  while(isspace(*argument))
    argument++;

  return argument;
}

/*
 * Pick off one argument from a string and return the rest.
 * Understands quotes.  Delimiters = { ' ', '-' }
 */
char                   *one_argument2(char *argument, char *arg_first)
{
  char                    cEnd;
  short                   count;

  count = 0;

  if(!VLD_STR(argument))
  {
    arg_first[0] = '\0';
    return argument;
  }

  while(isspace(*argument))
    argument++;

  cEnd = ' ';
  if(*argument == '\'' || *argument == '"')
    cEnd = *argument++;

  while(*argument != '\0' || ++count >= 255)
  {
    if(*argument == cEnd || *argument == '-')
    {
      argument++;
      break;
    }
    *arg_first = LOWER(*argument);
    arg_first++;
    argument++;
  }
  *arg_first = '\0';

  while(isspace(*argument))
    argument++;

  return argument;
}

void do_timecmd(CHAR_DATA *ch, char *argument)
{
  struct timeval          stime;
  struct timeval          etime;
  static bool             timing;

/*
  extern CHAR_DATA *timechar; */
  char                    arg[MIL];

  send_to_char("Timing\r\n", ch);
  if(timing)
    return;
  one_argument(argument, arg);
  if(!*arg)
  {
    send_to_char("No command to time.\r\n", ch);
    return;
  }
  if(!str_cmp(arg, "update"))
  {
    if(timechar)
      send_to_char("Another person is already timing updates.\r\n", ch);
    else
    {
      timechar = ch;
      send_to_char("Setting up to record next update loop.\r\n", ch);
    }
    return;
  }
  set_char_color(AT_PLAIN, ch);
  send_to_char("Starting timer.\r\n", ch);
  timing = TRUE;
  gettimeofday(&stime, NULL);
  interpret(ch, argument);
  gettimeofday(&etime, NULL);
  timing = FALSE;
  set_char_color(AT_PLAIN, ch);
  send_to_char("Timing complete.\r\n", ch);
  subtract_times(&etime, &stime);
  ch_printf(ch, "Timing took %ld %ld seconds.\r\n", etime.tv_sec, etime.tv_usec);
  return;
}

void start_timer(struct timeval *stime)
{
  if(!stime)
  {
    bug("%s", "Start_timer: NULL stime.");
    return;
  }
  gettimeofday(stime, NULL);
  return;
}

time_t end_timer(struct timeval * stime)
{
  struct timeval          etime;

  /*
   * Mark etime before checking stime, so that we get a better reading.. 
   */
  gettimeofday(&etime, NULL);
  if(!stime || (!stime->tv_sec && !stime->tv_usec))
  {
    bug("%s", "End_timer: bad stime.");
    return 0;
  }
  subtract_times(&etime, stime);
  /*
   * stime becomes time used 
   */
  *stime = etime;
  return (etime.tv_sec * 1000000) + etime.tv_usec;
}

void send_timer(struct timerset *vtime, CHAR_DATA *ch)
{
  struct timeval          ntime;
  int                     carry;

  if(vtime->num_uses == 0)
    return;
  ntime.tv_sec = vtime->total_time.tv_sec / vtime->num_uses;
  carry = (vtime->total_time.tv_sec % vtime->num_uses) * 1000000;
  ntime.tv_usec = (vtime->total_time.tv_usec + carry) / vtime->num_uses;
  ch_printf(ch, "Has been used %d times this boot.\r\n", vtime->num_uses);
  ch_printf(ch, "Time (in secs): min %ld %ld avg: %ld %ld max %ld %ld"
            "\r\n", vtime->min_time.tv_sec, vtime->min_time.tv_usec, ntime.tv_sec, ntime.tv_usec, vtime->max_time.tv_sec, vtime->max_time.tv_usec);
  return;
}

void update_userec(struct timeval *time_used, struct timerset *userec)
{
  userec->num_uses++;
  if(!timerisset(&userec->min_time) || timercmp(time_used, &userec->min_time, <))
  {
    userec->min_time.tv_sec = time_used->tv_sec;
    userec->min_time.tv_usec = time_used->tv_usec;
  }
  if(!timerisset(&userec->max_time) || timercmp(time_used, &userec->max_time, >))
  {
    userec->max_time.tv_sec = time_used->tv_sec;
    userec->max_time.tv_usec = time_used->tv_usec;
  }
  userec->total_time.tv_sec += time_used->tv_sec;
  userec->total_time.tv_usec += time_used->tv_usec;
  while(userec->total_time.tv_usec >= 1000000)
  {
    userec->total_time.tv_sec++;
    userec->total_time.tv_usec -= 1000000;
  }
  return;
}

/*
 *  This function checks the command against the command flags to make
 *  sure they can use the command online.  This allows the commands to be
 *  edited online to allow or disallow certain situations.  May be an idea
 *  to rework this so we can edit the message sent back online, as well as
 *  maybe a crude parsing language so we can add in new checks online without
 *  haveing to hard-code them in.     -- Shaddai   August 25, 1997
 */
char                   *check_cmd_flags(CHAR_DATA *ch, CMDTYPE * cmd)
{
  static char             cmd_flag_buf[MSL];

  if(!IS_NPC(ch) && IS_IMMORTAL(ch) && (get_trust(ch) < LEVEL_AJ_LT))
  {
    if(IS_SET(cmd->flags, CMD_ADMIN) || IS_SET(cmd->flags, CMD_ENFORCER) || IS_SET(cmd->flags, CMD_BUILDER))
    {
      bool                    fUseable = FALSE;

      if(IS_SET(cmd->flags, CMD_ADMIN) && IS_SET(ch->pcdata->flags, PCFLAG_ADMIN))
        fUseable = TRUE;

      if(IS_SET(cmd->flags, CMD_ENFORCER) && IS_SET(ch->pcdata->flags, PCFLAG_ENFORCER))
        fUseable = TRUE;

      if(IS_SET(cmd->flags, CMD_BUILDER) && IS_SET(ch->pcdata->flags, PCFLAG_BUILDER))
        fUseable = TRUE;

      if(fUseable == FALSE)
        snprintf(cmd_flag_buf, MSL, "%s is not a command allowed to you by your current status.\r\n", cmd->name);
      else
        cmd_flag_buf[0] = '\0';
    }
    else
      cmd_flag_buf[0] = '\0';
  }
  else if(IS_AFFECTED(ch, AFF_POSSESS) && IS_SET(cmd->flags, CMD_FLAG_POSSESS))
    snprintf(cmd_flag_buf, MSL, "You can't %s while you are possessing someone!\r\n", cmd->name);
  else if(ch->morph != NULL && IS_SET(cmd->flags, CMD_FLAG_POLYMORPHED))
    snprintf(cmd_flag_buf, MSL, "You can't %s while you are polymorphed!\r\n", cmd->name);
  else
    cmd_flag_buf[0] = '\0';

  return cmd_flag_buf;
}
