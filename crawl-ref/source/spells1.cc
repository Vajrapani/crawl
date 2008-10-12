/*
 *  File:       spells1.cc
 *  Summary:    Implementations of some additional spells.
 *              Mostly Translocations.
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 */

#include "AppHdr.h"
#include "spells1.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>

#include "externs.h"

#include "abyss.h"
#include "beam.h"
#include "cloud.h"
#include "describe.h"
#include "directn.h"
#include "effects.h"
#include "invent.h"
#include "it_use2.h"
#include "itemname.h"
#include "itemprop.h"
#include "message.h"
#include "misc.h"
#include "monplace.h"
#include "monstuff.h"
#include "mon-util.h"
#include "player.h"
#include "randart.h"
#include "religion.h"
#include "skills2.h"
#include "spells2.h"
#include "spells3.h"
#include "spells4.h"
#include "spl-util.h"
#include "state.h"
#include "stuff.h"
#include "terrain.h"
#include "transfor.h"
#include "traps.h"
#include "view.h"

static bool _abyss_blocks_teleport(bool cblink)
{
    // Lugonu worshippers get their perks.
    if (you.religion == GOD_LUGONU)
        return (false);

    // Controlled Blink (the spell) works quite reliably in the Abyss.
    return (cblink ? one_chance_in(3) : !one_chance_in(3));
}

// If wizard_blink is set, all restriction are ignored (except for
// a monster being at the target spot), and the player gains no
// contamination.
int blink(int pow, bool high_level_controlled_blink, bool wizard_blink)
{
    dist beam;

    if (crawl_state.is_repeating_cmd())
    {
        crawl_state.cant_cmd_repeat("You can't repeat controlled blinks.");
        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        return(1);
    }

    // yes, there is a logic to this ordering {dlb}:
    if (scan_randarts(RAP_PREVENT_TELEPORTATION) && !wizard_blink)
        mpr("You feel a weird sense of stasis.");
    else if (you.level_type == LEVEL_ABYSS
             && _abyss_blocks_teleport(high_level_controlled_blink)
             && !wizard_blink)
    {
        mpr("The power of the Abyss keeps you in your place!");
    }
    else if (you.confused() && !wizard_blink)
        random_blink(false);
    else if (!allow_control_teleport(true) && !wizard_blink)
    {
        mpr("A powerful magic interferes with your control of the blink.");
        if (high_level_controlled_blink)
            return (cast_semi_controlled_blink(pow));
        random_blink(false);
    }
    else
    {
        // query for location {dlb}:
        while (true)
        {
            direction(beam, DIR_TARGET, TARG_ANY, -1, false, false, false,
                     false, "Blink to where?");

            if (!beam.isValid || beam.target == you.pos())
            {
                if (!wizard_blink
                    && !yesno("Are you sure you want to cancel this blink?",
                              false, 'n'))
                {
                    mesclr();
                    continue;
                }
                canned_msg(MSG_OK);
                return (-1);         // early return {dlb}
            }

            if (!wizard_blink && you.duration[DUR_BEHELD])
            {
                bool blocked_movement = false;
                for (unsigned int i = 0; i < you.beheld_by.size(); i++)
                {
                    monsters& mon = menv[you.beheld_by[i]];
                    const int olddist = grid_distance(you.pos(), mon.pos());
                    const int newdist = grid_distance(beam.target, mon.pos());

                    if (olddist < newdist)
                    {
                        mprf("You cannot blink away from %s!",
                             mon.name(DESC_NOCAP_THE, true).c_str());

                        blocked_movement = true;
                        break;
                    }
                }

                if (blocked_movement)
                    continue;
            }

            // Wizard blink can move past translucent walls.
            if (see_grid_no_trans(beam.target))
                break;
            else if (trans_wall_blocking( beam.target ))
            {
                // Wizard blink can move past translucent walls.
                if (wizard_blink)
                    break;

                mesclr();
                mpr("You can't blink through translucent walls.");
            }
            else
            {
                mesclr();
                mpr("You can only blink to visible locations.");
            }
        }

        // Allow wizard blink to send player into walls, in case the
        // user wants to alter that grid to something else.
        if (wizard_blink && grid_is_solid(grd(beam.target)))
            grd(beam.target) = DNGN_FLOOR;

        if (grid_is_solid(grd(beam.target))
            || mgrd(beam.target) != NON_MONSTER)
        {
            mpr("Oops! Maybe something was there already.");
            random_blink(false);
        }
        else if (you.level_type == LEVEL_ABYSS && !wizard_blink)
        {
            abyss_teleport( false );
            if (you.pet_target != MHITYOU)
                you.pet_target = MHITNOT;
        }
        else
        {
            // no longer held in net
            clear_trapping_net();

            move_player_to_grid(beam.target, false, true, true);

            // controlling teleport contaminates the player -- bwr
            if (!wizard_blink)
                contaminate_player( 1, true );
        }

        if (you.duration[DUR_CONDENSATION_SHIELD] > 0 && !wizard_blink)
        {
            you.duration[DUR_CONDENSATION_SHIELD] = 0;
            you.redraw_armour_class = true;
        }
    }

    crawl_state.cancel_cmd_again();
    crawl_state.cancel_cmd_repeat();

    return (1);
}                               // end blink()

void random_blink(bool allow_partial_control, bool override_abyss)
{
    coord_def target;
    bool succ = false;

    if (scan_randarts(RAP_PREVENT_TELEPORTATION))
        mpr("You feel a weird sense of stasis.");
    else if (you.level_type == LEVEL_ABYSS
             && !override_abyss && !one_chance_in(3))
    {
        mpr("The power of the Abyss keeps you in your place!");
    }
    // First try to find a random square not adjacent to the player,
    // then one adjacent if that fails.
    else if (!random_near_space(you.pos(), target)
             && !random_near_space(you.pos(), target, true))
    {
        mpr("You feel jittery for a moment.");
    }

#ifdef USE_SEMI_CONTROLLED_BLINK
    //jmf: Add back control, but effect is cast_semi_controlled_blink(pow).
    else if (player_control_teleport() && !you.confused()
             && allow_partial_control && allow_control_teleport())
    {
        mpr("You may select the general direction of your translocation.");
        cast_semi_controlled_blink(100);
        succ = true;
    }
#endif

    else
    {
        mpr("You blink.");

        // No longer held in net.
        clear_trapping_net();

        succ = true;
        you.moveto(target);

        if (you.level_type == LEVEL_ABYSS)
        {
            abyss_teleport( false );
            if (you.pet_target != MHITYOU)
                you.pet_target = MHITNOT;
        }
    }

    if (succ && you.duration[DUR_CONDENSATION_SHIELD] > 0)
    {
        you.duration[DUR_CONDENSATION_SHIELD] = 0;
        you.redraw_armour_class = true;
    }
}

bool fireball(int pow, bolt &beam)
{
    return (zapping(ZAP_FIREBALL, pow, beam, true));
}

void setup_fire_storm(const actor *source, int pow, bolt &beam)
{
    beam.name         = "great blast of fire";
    beam.ex_size      = 2 + (random2(pow) > 75);
    beam.flavour      = BEAM_LAVA;
    beam.type         = dchar_glyph(DCHAR_FIRED_ZAP);
    beam.colour       = RED;
    beam.beam_source  = source->mindex();
    // XXX: Should this be KILL_MON_MISSILE?
    beam.thrower      =
        source->atype() == ACT_PLAYER? KILL_YOU_MISSILE : KILL_MON;
    beam.aux_source.clear();
    beam.obvious_effect = false;
    beam.is_beam      = false;
    beam.is_tracer    = false;
    beam.is_explosion = true;
    beam.ench_power   = pow;      // used for radius
    beam.hit          = 20 + pow / 10;
    beam.damage       = calc_dice(8, 5 + pow);
}

void cast_fire_storm(int pow, bolt &beam)
{
    setup_fire_storm(&you, pow, beam);

    if (explosion(beam, false, false, true, true, false) > 0)
        mpr("A raging storm of fire appears!");

    viewwindow(true, false);
}

void cast_chain_lightning(int pow)
{
    bolt beam;

    // initialize beam structure
    beam.name           = "lightning arc";
    beam.aux_source     = "chain lightning";
    beam.beam_source    = MHITYOU;
    beam.thrower        = KILL_YOU_MISSILE;
    beam.range          = 8;
    beam.hit            = AUTOMATIC_HIT;
    beam.type           = dchar_glyph(DCHAR_FIRED_ZAP);
    beam.flavour        = BEAM_ELECTRICITY;
    beam.obvious_effect = true;
    beam.is_beam        = false;       // since we want to stop at our target
    beam.is_explosion   = false;
    beam.is_tracer      = false;

    coord_def source, target;

    for (source = you.pos(); pow > 0; pow -= 8 + random2(13), source = target)
    {
        // infinity as far as this spell is concerned
        // (Range - 1) is used because the distance is randomized and
        // may be shifted by one.
        int min_dist = MONSTER_LOS_RANGE - 1;

        int dist;
        int count = 0;

        target.x = -1;
        target.y = -1;

        for (int i = 0; i < MAX_MONSTERS; i++)
        {
            monsters *monster = &menv[i];

            if (invalid_monster(monster))
                continue;

            dist = grid_distance( source, monster->pos() );

            // check for the source of this arc
            if (!dist)
                continue;

            // randomize distance (arcs don't care about a couple of feet)
            dist += (random2(3) - 1);

            // always ignore targets further than current one
            if (dist > min_dist)
                continue;

            if (!check_line_of_sight( source, monster->pos() ))
                continue;

            count++;

            if (dist < min_dist)
            {
                // switch to looking for closer targets (but not always)
                if (!one_chance_in(10))
                {
                    min_dist = dist;
                    target = monster->pos();
                    count = 0;
                }
            }
            else if (target.x == -1 || one_chance_in(count))
            {
                // either first target, or new selected target at min_dist
                target = monster->pos();

                // need to set min_dist for first target case
                dist = std::max(dist, min_dist);
            }
        }

        // now check if the player is a target
        dist = grid_distance(source, you.pos());

        if (dist)       // i.e., player was not the source
        {
            // distance randomized (as above)
            dist += (random2(3) - 1);

            // select player if only, closest, or randomly selected
            if ((target.x == -1
                    || dist < min_dist
                    || (dist == min_dist && one_chance_in(count + 1)))
                && check_line_of_sight(source, you.pos()))
            {
                target = you.pos();
            }
        }

        const bool see_source = see_grid( source );
        const bool see_targ   = see_grid( target );

        if (target.x == -1)
        {
            if (see_source)
                mpr("The lightning grounds out.");

            break;
        }

        // Trying to limit message spamming here so we'll only mention
        // the thunder when it's out of LoS.
        if (!see_source)
            noisy(25, source, "You hear a mighty clap of thunder!");

        if (see_source && !see_targ)
            mpr("The lightning arcs out of your line of sight!");
        else if (!see_source && see_targ)
            mpr("The lightning arc suddenly appears!");

        if (!see_grid_no_trans( target ))
        {
            // It's no longer in the caster's LOS and influence.
            pow = pow / 2 + 1;
        }

        beam.source = source;
        beam.target = target;
        beam.colour = LIGHTBLUE;
        beam.damage = calc_dice(5, 12 + pow * 2 / 3);

        // Be kinder to the player.
        if (target == you.pos())
        {
            if (!(beam.damage.num /= 2))
                beam.damage.num = 1;
            if ((beam.damage.size /= 2) < 3)
                beam.damage.size = 3;
        }
        fire_beam( beam );
    }

    more();
}

void identify(int power, int item_slot)
{
    int id_used = 1;

    // Scrolls of identify *may* produce "extra" identifications.
    if (power == -1 && one_chance_in(5))
        id_used += (coinflip()? 1 : 2);

    do
    {
        if (item_slot == -1)
        {
            item_slot = prompt_invent_item( "Identify which item?", MT_INVLIST,
                                            OSEL_UNIDENT, true, true, false );
        }
        if (prompt_failed(item_slot))
            return;

        item_def& item(you.inv[item_slot]);

        if (fully_identified(item))
        {
            mpr("Choose an unidentified item, or Esc to abort.");
            if (Options.auto_list)
                more();
            item_slot = -1;
            continue;
        }

        set_ident_type( item, ID_KNOWN_TYPE );
        set_ident_flags( item, ISFLAG_IDENT_MASK );
        if (Options.autoinscribe_randarts && is_random_artefact(item))
            add_autoinscription( item, randart_auto_inscription(item));

        // For scrolls, now id the scroll, unless already known.
        if (power == -1
            && get_ident_type(OBJ_SCROLLS, SCR_IDENTIFY) != ID_KNOWN_TYPE)
        {
            set_ident_type(OBJ_SCROLLS, SCR_IDENTIFY, ID_KNOWN_TYPE);

            const int wpn = you.equip[EQ_WEAPON];
            if (wpn != -1
                && you.inv[wpn].base_type == OBJ_SCROLLS
                && you.inv[wpn].sub_type == SCR_IDENTIFY)
            {
                you.wield_change = true;
            }
        }

        // Output identified item.
        mpr(item.name(DESC_INVENTORY_EQUIP).c_str());
        if (item_slot == you.equip[EQ_WEAPON])
            you.wield_change = true;

        id_used--;

        if (Options.auto_list && id_used > 0)
            more();

        // In case we get to try again.
        item_slot = -1;
    }
    while (id_used > 0);
}

// Returns whether the spell was actually cast.
bool conjure_flame(int pow)
{
    struct dist spelld;

    bool done_first_message = false;

    while (true)
    {
        if (done_first_message)
            mpr("Where would you like to place the cloud?", MSGCH_PROMPT);
        else
        {
            mpr("You cast a flaming cloud spell! But where?", MSGCH_PROMPT);
            done_first_message = true;
        }

        direction( spelld, DIR_TARGET, TARG_ENEMY, -1, false, false, false );

        if (!spelld.isValid)
        {
            canned_msg(MSG_OK);
            return (false);
        }

        if (trans_wall_blocking(spelld.target))
        {
            mpr("A translucent wall is in the way.");
            return (false);
        }
        else if (!see_grid(spelld.target))
        {
            mpr("You can't see that place!");
            continue;
        }

        if (spelld.target == you.pos())
        {
            mpr("You can't place the cloud here!");
            continue;
        }

        const int cloud = env.cgrid(spelld.target);

        if (grid_is_solid(grd(spelld.target)) ||
            mgrd(spelld.target) != NON_MONSTER ||
            (cloud != EMPTY_CLOUD && env.cloud[cloud].type != CLOUD_FIRE))
        {
            mpr( "There's already something there!" );
            continue;
        }
        else if (cloud != EMPTY_CLOUD)
        {
            // Reinforce the cloud - but not too much.
            mpr( "The fire roars with new energy!" );
            const int extra_dur = 2 + std::min(random2(pow) / 2, 20);
            env.cloud[cloud].decay += extra_dur * 5;
            env.cloud[cloud].set_whose(KC_YOU);
            return (true);
        }

        break;
    }

    int durat = 5 + (random2(pow) / 2) + (random2(pow) / 2);

    if (durat > 23)
        durat = 23;

    place_cloud( CLOUD_FIRE, spelld.target, durat, KC_YOU );
    return (true);
}

bool stinking_cloud( int pow, bolt &beem )
{
    beem.name        = "ball of vapour";
    beem.colour      = GREEN;
    beem.range       = 6;
    beem.damage      = dice_def( 1, 0 );
    beem.hit         = 20;
    beem.type        = dchar_glyph(DCHAR_FIRED_ZAP);
    beem.ench_power  = pow;
    beem.beam_source = MHITYOU;
    beem.thrower     = KILL_YOU;
    beem.is_beam     = false;
    beem.aux_source.clear();

    // Don't bother tracing if you're targetting yourself.
    if (beem.target != you.pos())
    {
        // Fire tracer.
        beem.source        = you.pos();
        beem.can_see_invis = player_see_invis();
        beem.smart_monster = true;
        beem.attitude      = ATT_FRIENDLY;
        beem.fr_count      = 0;
        beem.flavour       = BEAM_POTION_STINKING_CLOUD;
        beem.is_tracer     = true;
        fire_beam(beem);

        if (beem.beam_cancelled)
        {
            // We don't want to fire through friendlies.
            canned_msg(MSG_OK);
            return (false);
        }
    }

    // Really fire.
    beem.flavour   = BEAM_MMISSILE;
    beem.is_tracer = false;
    fire_beam(beem);

    return (true);
}

int cast_big_c(int pow, cloud_type cty, kill_category whose, bolt &beam)
{
    big_cloud( cty, whose, beam.target, pow, 8 + random2(3) );
    return (1);
}

void big_cloud(cloud_type cl_type, kill_category whose,
               const coord_def& where, int pow, int size, int spread_rate)
{
    big_cloud(cl_type, whose, cloud_struct::whose_to_killer(whose),
              where, pow, size, spread_rate);
}

void big_cloud(cloud_type cl_type, killer_type killer,
               const coord_def& where, int pow, int size, int spread_rate)
{
    big_cloud(cl_type, cloud_struct::killer_to_whose(killer), killer,
              where, pow, size, spread_rate);
}

void big_cloud(cloud_type cl_type, kill_category whose, killer_type killer,
               const coord_def& where, int pow, int size, int spread_rate)
{
    apply_area_cloud(make_a_normal_cloud, where, pow, size,
                     cl_type, whose, killer, spread_rate);
}

static bool _mons_hostile(const monsters *mon)
{
    // Needs to be done this way because of friendly/neutral enchantments.
    return (!mons_wont_attack(mon) && !mons_neutral(mon));
}

static bool _can_pacify_monster(const monsters *mon, const int healed)
{
    ASSERT(you.religion == GOD_ELYVILON);

    if (healed < 1)
        return (false);

    // I was thinking of jellies when I wrote this, but maybe we shouldn't
    // exclude zombies and such... (jpeg)
    if (mons_intel(mon) <= I_PLANT) // no self-awareness
        return (false);

    const mon_holy_type holiness = mons_holiness(mon);

    if (holiness != MH_HOLY
        && holiness != MH_NATURAL
        && holiness != MH_UNDEAD
        && holiness != MH_DEMONIC)
    {
        return (false);
    }

    if (mons_is_stationary(mon)) // not able to leave the level
        return (false);

    if (mons_is_sleeping(mon)) // not aware of what is happening
        return (false);

    const int factor = (mons_intel(mon) <= I_ANIMAL)       ? 3 : // animals
                       (is_player_same_species(mon->type)) ? 2   // same species
                                                           : 1;  // other

    int divisor = 3;

    if (holiness == MH_HOLY)
        divisor--;
    else if (holiness == MH_UNDEAD)
        divisor++;
    else if (holiness == MH_DEMONIC)
        divisor += 2;

    const int random_factor = random2(you.skills[SK_INVOCATIONS] * healed /
                                      divisor);

#ifdef DEBUG_DIAGNOSTICS
    mprf(MSGCH_DIAGNOSTICS,
         "pacifying %s? max hp: %d, factor: %d, Inv: %d, healed: %d, rnd: %d",
         mon->name(DESC_PLAIN).c_str(), mon->max_hit_points, factor,
         you.skills[SK_INVOCATIONS], healed, random_factor);
#endif

    if (mon->max_hit_points < factor * random_factor)
        return (true);

    return (false);
}

static int _healing_spell(int healed, const coord_def where = coord_def(0,0))
{
    ASSERT(healed >= 1);

    bolt beam;
    dist spd;

    if (where.origin())
    {
        spd.isValid = spell_direction(spd, beam, DIR_TARGET,
                                      you.religion == GOD_ELYVILON ?
                                          TARG_ANY : TARG_FRIEND,
                                      LOS_RADIUS,
                                      true, true, true, "Heal whom?");
    }
    else
    {
        spd.target = where;
        spd.isValid = in_bounds(spd.target);
    }

    if (!spd.isValid)
    {
        canned_msg(MSG_OK);
        return (0);
    }

    if (spd.target == you.pos())
    {
        mpr("You are healed.");
        inc_hp(healed, false);
        return (1);
    }

    const int mgr = mgrd(spd.target);

    if (mgr == NON_MONSTER)
    {
        mpr("There isn't anything there!");
        return (-1);
    }

    monsters *monster = &menv[mgr];

    // Don't heal a monster you can't pacify.
    if (you.religion == GOD_ELYVILON && !_can_pacify_monster(monster, healed))
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return (-1);
    }

    bool nothing_happens = true;
    if (heal_monster(monster, healed, false))
    {
        mprf("You heal %s.", monster->name(DESC_NOCAP_THE).c_str());

        if (monster->hit_points == monster->max_hit_points)
            simple_monster_message(monster, " is completely healed.");
        else
            print_wounds(monster);

        if (you.religion == GOD_ELYVILON && !_mons_hostile(monster))
        {
            simple_god_message(" appreciates the healing of a fellow creature.");
            if (one_chance_in(8))
                gain_piety(1);
            return (1);
        }

        nothing_happens = false;
    }

    if (you.religion == GOD_ELYVILON && _mons_hostile(monster))
    {
        simple_god_message(" supports your offer of peace.");

        if (mons_is_holy(monster))
            good_god_holy_attitude_change(monster);
        else
        {
            simple_monster_message(monster, " turns neutral.");
            mons_pacify(monster);

            // Give a small piety return.
            gain_piety(1 + random2(healed/15));
        }
    }
    else if (nothing_happens)
        canned_msg(MSG_NOTHING_HAPPENS);

    return (1);
}

#if 0
char cast_lesser_healing( int pow )
{
    return _healing_spell(5 + random2avg(7, 2));
}

char cast_greater_healing( int pow )
{
    return _healing_spell(15 + random2avg(29, 2));
}

char cast_greatest_healing( int pow )
{
    return _healing_spell(50 + random2avg(49, 2));
}
#endif

int cast_healing(int pow, const coord_def& where)
{
    pow = std::min(50, pow);

    return (_healing_spell(pow + roll_dice(2, pow) - 2, where));
}

void remove_divine_vigour()
{
    mpr("Your divine vigour fades.", MSGCH_DURATION);
    you.duration[DUR_DIVINE_VIGOUR] = 0;
    you.attribute[ATTR_DIVINE_VIGOUR] = 0;
    calc_hp();
}

bool cast_divine_vigour()
{
    bool success = false;

    if (!you.duration[DUR_DIVINE_VIGOUR])
    {
        mprf("%s grants you divine vigour.",
             god_name(you.religion).c_str());

        const int vigour_amt = 1 + (you.skills[SK_INVOCATIONS]/6);
        const int old_hp_max = you.hp_max;
        you.attribute[ATTR_DIVINE_VIGOUR] = vigour_amt;
        you.duration[DUR_DIVINE_VIGOUR]
            = 35 + (you.skills[SK_INVOCATIONS]*5)/3;
        calc_hp();
        inc_hp(you.hp_max - old_hp_max, false);

        success = true;
    }
    else
        canned_msg(MSG_NOTHING_HAPPENS);

    return (success);
}

void remove_divine_stamina()
{
    mpr("Your divine stamina fades.", MSGCH_DURATION);
    modify_stat(STAT_STRENGTH, -you.attribute[ATTR_DIVINE_STAMINA],
                true, "Zin's divine stamina running out");
    modify_stat(STAT_INTELLIGENCE, -you.attribute[ATTR_DIVINE_STAMINA],
                true, "Zin's divine stamina running out");
    modify_stat(STAT_DEXTERITY, -you.attribute[ATTR_DIVINE_STAMINA],
                true, "Zin's divine stamina running out");
    you.attribute[ATTR_DIVINE_STAMINA] = 0;
}

bool cast_vitalisation()
{
    bool success = false;
    int type = 0;

    // Remove negative afflictions.
    if (you.disease || you.rotting || you.confused()
        || you.duration[DUR_PARALYSIS] || you.duration[DUR_POISONING]
        || you.duration[DUR_PETRIFIED])
    {
        do
        {
            switch (random2(6))
            {
            case 0:
                if (you.disease)
                {
                    success = true;
                    you.disease = 0;
                }
                break;
            case 1:
                if (you.rotting)
                {
                    success = true;
                    you.rotting = 0;
                }
                break;
            case 2:
                if (you.duration[DUR_CONF])
                {
                    success = true;
                    you.duration[DUR_CONF] = 0;
                }
                break;
            case 3:
                if (you.duration[DUR_PARALYSIS])
                {
                    success = true;
                    you.duration[DUR_PARALYSIS] = 0;
                }
                break;
            case 4:
                if (you.duration[DUR_POISONING])
                {
                    success = true;
                    you.duration[DUR_POISONING] = 0;
                }
                break;
            case 5:
                if (you.duration[DUR_PETRIFIED])
                {
                    success = true;
                    you.duration[DUR_PETRIFIED] = 0;
                }
                break;
            }
        }
        while (!success);
    }
    // Restore stats.
    else if (you.strength < you.max_strength
        || you.intel < you.max_intel
        || you.dex < you.max_dex)
    {
        type = 1;

        do
        {
            switch (random2(3))
            {
            case 0:
                if (you.strength < you.max_strength)
                {
                    success = true;
                    restore_stat(STAT_STRENGTH, 0, true);
                }
                break;
            case 1:
                if (you.intel < you.max_intel)
                {
                    success = true;
                    restore_stat(STAT_INTELLIGENCE, 0, true);
                }
                break;
            case 2:
                if (you.dex < you.max_dex)
                {
                    success = true;
                    restore_stat(STAT_DEXTERITY, 0, true);
                }
                break;
            }
        }
        while (!success);
    }
    else
    {
        // Add divine stamina.
        if (!you.duration[DUR_DIVINE_STAMINA])
        {
            success = true;
            type = 2;

            mprf("%s grants you divine stamina.",
                 god_name(you.religion).c_str());

            const int stamina_amt = 3;
            you.attribute[ATTR_DIVINE_STAMINA] = stamina_amt;
            you.duration[DUR_DIVINE_STAMINA]
                = 35 + (you.skills[SK_INVOCATIONS]*5)/3;

            modify_stat(STAT_STRENGTH, stamina_amt, true, "");
            modify_stat(STAT_INTELLIGENCE, stamina_amt, true, "");
            modify_stat(STAT_DEXTERITY, stamina_amt, true, "");
        }
    }

    // If vitalisation has succeeded, display an appropriate message.
    if (success)
    {
        mprf("You feel %s.", (type == 0) ? "better" :
                             (type == 1) ? "renewed"
                                         : "powerful");
    }
    else
        canned_msg(MSG_NOTHING_HAPPENS);

    return (success);
}

bool cast_revivification(int pow)
{
    int loopy = 0;              // general purpose loop variable {dlb}
    bool success = false;
    int loss = 0;

    if (you.hp == you.hp_max)
        canned_msg(MSG_NOTHING_HAPPENS);
    else if (you.hp_max < 21)
        mpr("You lack the resilience to cast this spell.");
    else
    {
        mpr("Your body is healed in an amazingly painful way.");

        loss = 2;
        for (loopy = 0; loopy < 9; loopy++)
            if (x_chance_in_y(8, pow))
                loss++;

        dec_max_hp(loss);
        set_hp(you.hp_max, false);
        success = true;
    }

    return (success);
}

void cast_cure_poison(int mabil)
{
    if (!you.duration[DUR_POISONING])
        canned_msg(MSG_NOTHING_HAPPENS);
    else
        reduce_poison_player( 2 + random2(mabil) + random2(3) );

    return;
}

void purification(void)
{
    mpr("You feel purified!");

    you.disease = 0;
    you.rotting = 0;
    you.duration[DUR_POISONING] = 0;
    you.duration[DUR_CONF] = 0;
    you.duration[DUR_SLOW] = 0;
    you.duration[DUR_PARALYSIS] = 0;          // can't currently happen -- bwr
    you.duration[DUR_PETRIFIED] = 0;
}

int allowed_deaths_door_hp(void)
{
    int hp = you.skills[SK_NECROMANCY] / 2;

    if (you.religion == GOD_KIKUBAAQUDGHA && !player_under_penance())
        hp += you.piety / 15;

    return (hp);
}

void cast_deaths_door(int pow)
{
    if (you.is_undead)
        mpr("You're already dead!");
    else if (you.duration[DUR_DEATHS_DOOR])
        mpr("Your appeal for an extension has been denied.");
    else
    {
        mpr("You feel invincible!");
        mpr("You seem to hear sand running through an hourglass...",
            MSGCH_SOUND);

        set_hp( allowed_deaths_door_hp(), false );
        deflate_hp( you.hp_max, false );

        you.duration[DUR_DEATHS_DOOR] = 10 + random2avg(13, 3)
                                           + (random2(pow) / 10);

        if (you.duration[DUR_DEATHS_DOOR] > 25)
            you.duration[DUR_DEATHS_DOOR] = 23 + random2(5);
    }

    return;
}

void abjuration(int pow)
{
    mpr("Send 'em back where they came from!");

    // Scale power into something comparable to summon lifetime.
    const int abjdur = pow * 12;

    for (int i = 0; i < MAX_MONSTERS; ++i)
    {
        monsters* const monster = &menv[i];

        if (monster->type == -1 || !mons_near(monster))
            continue;

        if (mons_wont_attack(monster))
            continue;

        mon_enchant abj = monster->get_ench(ENCH_ABJ);
        if (abj.ench != ENCH_NONE)
        {
            int sockage = std::max(fuzz_value(abjdur, 60, 30), 40);
#ifdef DEBUG_DIAGNOSTICS
            mprf(MSGCH_DIAGNOSTICS, "%s abj: dur: %d, abj: %d",
                 monster->name(DESC_PLAIN).c_str(), abj.duration, sockage);
#endif

            // TSO and Trog's abjuration protection.
            if (mons_is_god_gift(monster, GOD_SHINING_ONE))
            {
                sockage = sockage * (30 - monster->hit_dice) / 45;
                if (sockage < abj.duration)
                {
                    simple_god_message(" protects a fellow warrior from your evil magic!",
                                       GOD_SHINING_ONE);
                }
            }
            else if (mons_is_god_gift(monster, GOD_TROG))
            {
                sockage = sockage * 8 / 15;
                if (sockage < abj.duration)
                {
                    simple_god_message(" shields an ally from your puny magic!",
                                       GOD_TROG);
                }
            }

            if (!monster->lose_ench_duration(abj, sockage))
                simple_monster_message(monster, " shudders.");
        }
    }
}

// Antimagic is sort of an anti-extension... it sets a lot of magical
// durations to 1 so it's very nasty at times (and potentially lethal,
// that's why we reduce levitation to 2, so that the player has a chance
// to stop insta-death... sure the others could lead to death, but that's
// not as direct as falling into deep water) -- bwr
void antimagic()
{
    duration_type dur_list[] = {
        DUR_INVIS, DUR_CONF, DUR_PARALYSIS, DUR_SLOW, DUR_HASTE,
        DUR_MIGHT, DUR_FIRE_SHIELD, DUR_ICY_ARMOUR, DUR_REPEL_MISSILES,
        DUR_REGENERATION, DUR_SWIFTNESS, DUR_STONEMAIL, DUR_CONTROL_TELEPORT,
        DUR_TRANSFORMATION, DUR_DEATH_CHANNEL, DUR_DEFLECT_MISSILES,
        DUR_FORESCRY, DUR_SEE_INVISIBLE, DUR_WEAPON_BRAND, DUR_SILENCE,
        DUR_CONDENSATION_SHIELD, DUR_STONESKIN, DUR_BARGAIN,
        DUR_INSULATION, DUR_RESIST_POISON, DUR_RESIST_FIRE, DUR_RESIST_COLD,
        DUR_SLAYING, DUR_STEALTH, DUR_MAGIC_SHIELD, DUR_SAGE, DUR_PETRIFIED
    };

    if (!you.permanent_levitation() && !you.permanent_flight()
        && you.duration[DUR_LEVITATION] > 2)
    {
        you.duration[DUR_LEVITATION] = 2;
    }

    if (!you.permanent_flight() && you.duration[DUR_CONTROLLED_FLIGHT] > 1)
        you.duration[DUR_CONTROLLED_FLIGHT] = 1;

    for ( unsigned int i = 0; i < ARRAYSZ(dur_list); ++i )
        if ( you.duration[dur_list[i]] > 1 )
            you.duration[dur_list[i]] = 1;

    contaminate_player( -1 * (1 + random2(5)));
}                               // end antimagic()

void extension(int pow)
{
    int contamination = random2(2);

    if (you.duration[DUR_HASTE])
    {
        potion_effect(POT_SPEED, pow);
        contamination++;
    }

    if (you.duration[DUR_SLOW])
        potion_effect(POT_SLOWING, pow);

    if (you.duration[DUR_MIGHT])
    {
        potion_effect(POT_MIGHT, pow);
        contamination++;
    }

    if (you.duration[DUR_LEVITATION] && !you.duration[DUR_CONTROLLED_FLIGHT])
        potion_effect(POT_LEVITATION, pow);

    if (you.duration[DUR_INVIS])
    {
        potion_effect(POT_INVISIBILITY, pow);
        contamination++;
    }

    if (you.duration[DUR_ICY_ARMOUR])
        ice_armour(pow, true);

    if (you.duration[DUR_REPEL_MISSILES])
        missile_prot(pow);

    if (you.duration[DUR_REGENERATION])
        cast_regen(pow);

    if (you.duration[DUR_DEFLECT_MISSILES])
        deflection(pow);

    if (you.duration[DUR_FIRE_SHIELD])
    {
        you.duration[DUR_FIRE_SHIELD] += random2(pow / 20);

        if (you.duration[DUR_FIRE_SHIELD] > 50)
            you.duration[DUR_FIRE_SHIELD] = 50;

        mpr("Your ring of flames roars with new vigour!");
    }

    if ( !(you.duration[DUR_WEAPON_BRAND] < 1
                 || you.duration[DUR_WEAPON_BRAND] > 80) )
    {
        you.duration[DUR_WEAPON_BRAND] += 5 + random2(8);
    }

    if (you.duration[DUR_SWIFTNESS])
        cast_swiftness(pow);

    if (you.duration[DUR_INSULATION])
        cast_insulation(pow);

    if (you.duration[DUR_STONEMAIL])
        stone_scales(pow);

    if (you.duration[DUR_CONTROLLED_FLIGHT])
        cast_fly(pow);

    if (you.duration[DUR_CONTROL_TELEPORT])
        cast_teleport_control(pow);

    if (you.duration[DUR_RESIST_POISON])
        cast_resist_poison(pow);

    if (you.duration[DUR_TRANSFORMATION])
    {
        mpr("Your transformation has been extended.");
        you.duration[DUR_TRANSFORMATION] += random2(pow);
        if (you.duration[DUR_TRANSFORMATION] > 100)
            you.duration[DUR_TRANSFORMATION] = 100;
    }

    //jmf: added following
    if (you.duration[DUR_STONESKIN])
        cast_stoneskin(pow);

    if (you.duration[DUR_FORESCRY])
        cast_forescry(pow);

    if (you.duration[DUR_SEE_INVISIBLE])
        cast_see_invisible(pow);

    if (you.duration[DUR_SILENCE])   //how precisely did you cast extension?
        cast_silence(pow);

    if (you.duration[DUR_CONDENSATION_SHIELD])
        cast_condensation_shield(pow);

    if (contamination)
        contaminate_player( contamination, true );
}                               // end extension()

void ice_armour(int pow, bool extending)
{
    if (!player_light_armour())
    {
        if (!extending)
            mpr("You are wearing too much armour.");

        return;
    }

    if (you.duration[DUR_STONEMAIL] || you.duration[DUR_STONESKIN])
    {
        if (!extending)
            mpr("The spell conflicts with another spell still in effect.");

        return;
    }

    if (you.duration[DUR_ICY_ARMOUR])
        mpr( "Your icy armour thickens." );
    else
    {
        if (you.attribute[ATTR_TRANSFORMATION] == TRAN_ICE_BEAST)
            mpr( "Your icy body feels more resilient." );
        else
            mpr( "A film of ice covers your body!" );

        you.redraw_armour_class = true;
    }

    you.duration[DUR_ICY_ARMOUR] += 20 + random2(pow) + random2(pow);

    if (you.duration[DUR_ICY_ARMOUR] > 50)
        you.duration[DUR_ICY_ARMOUR] = 50;
}                               // end ice_armour()

void stone_scales(int pow)
{
    int dur_change = 0;

    if (you.duration[DUR_ICY_ARMOUR] || you.duration[DUR_STONESKIN])
    {
        mpr("The spell conflicts with another spell still in effect.");
        return;
    }

    if (you.duration[DUR_STONEMAIL])
        mpr("Your scaly armour looks firmer.");
    else
    {
        if (you.attribute[ATTR_TRANSFORMATION] == TRAN_STATUE)
            mpr( "Your stone body feels more resilient." );
        else
            mpr( "A set of stone scales covers your body!" );

        you.redraw_evasion = true;
        you.redraw_armour_class = true;
    }

    dur_change = 20 + random2(pow) + random2(pow);

    if (dur_change + you.duration[DUR_STONEMAIL] >= 100)
        you.duration[DUR_STONEMAIL] = 100;
    else
        you.duration[DUR_STONEMAIL] += dur_change;

    burden_change();
}                               // end stone_scales()

void missile_prot(int pow)
{
    mpr("You feel protected from missiles.");

    you.duration[DUR_REPEL_MISSILES] += 8 + roll_dice( 2, pow );

    if (you.duration[DUR_REPEL_MISSILES] > 100)
        you.duration[DUR_REPEL_MISSILES] = 100;
}

void deflection(int pow)
{
    mpr("You feel very safe from missiles.");

    you.duration[DUR_DEFLECT_MISSILES] += 15 + random2(pow);

    if (you.duration[DUR_DEFLECT_MISSILES] > 100)
        you.duration[DUR_DEFLECT_MISSILES] = 100;
}

void cast_regen(int pow)
{
    //if (pow > 150) pow = 150;
    mpr("Your skin crawls.");

    you.duration[DUR_REGENERATION] += 5 + roll_dice( 2, pow / 3 + 1 );

    if (you.duration[DUR_REGENERATION] > 100)
        you.duration[DUR_REGENERATION] = 100;
}

void cast_berserk(void)
{
    go_berserk(true);
}

void cast_swiftness(int power)
{
    int dur_incr = 0;

    if (player_in_water())
    {
        mpr("The water foams!");
        return;
    }

    if (!you.duration[DUR_SWIFTNESS] && player_movement_speed() <= 6)
    {
        mpr( "You can't move any more quickly." );
        return;
    }

    // Reduced the duration:  -- bwr
    // dur_incr = random2(power) + random2(power) + 20;
    dur_incr = 20 + random2( power );

    // [dshaligram] Removed the on-your-feet bit.  Sounds odd when
    // you're levitating, for instance.
    mpr("You feel quick.");

    if (dur_incr + you.duration[DUR_SWIFTNESS] > 100)
        you.duration[DUR_SWIFTNESS] = 100;
    else
        you.duration[DUR_SWIFTNESS] += dur_incr;
}

void cast_fly(int power)
{
    const int dur_change = 25 + random2(power) + random2(power);

    const bool was_levitating = player_is_airborne();

    if (you.duration[DUR_LEVITATION] + dur_change > 100)
        you.duration[DUR_LEVITATION] = 100;
    else
        you.duration[DUR_LEVITATION] += dur_change;

    if (you.duration[DUR_CONTROLLED_FLIGHT] + dur_change > 100)
        you.duration[DUR_CONTROLLED_FLIGHT] = 100;
    else
        you.duration[DUR_CONTROLLED_FLIGHT] += dur_change;

    burden_change();

    if (!was_levitating)
    {
        if (you.light_flight())
            mpr("You swoop lightly up into the air.");
        else
            mpr("You fly up into the air.");
    }
    else
        mpr("You feel more buoyant.");
}

void cast_insulation(int power)
{
    int dur_incr = 10 + random2(power);

    mpr("You feel insulated.");

    if (dur_incr + you.duration[DUR_INSULATION] > 100)
        you.duration[DUR_INSULATION] = 100;
    else
        you.duration[DUR_INSULATION] += dur_incr;
}

void cast_resist_poison(int power)
{
    int dur_incr = 10 + random2(power);

    mpr("You feel resistant to poison.");

    if (dur_incr + you.duration[DUR_RESIST_POISON] > 100)
        you.duration[DUR_RESIST_POISON] = 100;
    else
        you.duration[DUR_RESIST_POISON] += dur_incr;
}

void cast_teleport_control(int power)
{
    int dur_incr = 10 + random2(power);

    mpr("You feel in control.");

    if (dur_incr + you.duration[DUR_CONTROL_TELEPORT] >= 50)
        you.duration[DUR_CONTROL_TELEPORT] = 50;
    else
        you.duration[DUR_CONTROL_TELEPORT] += dur_incr;
}

void cast_ring_of_flames(int power)
{
    you.duration[DUR_FIRE_SHIELD] += 5 + (power / 10) + (random2(power) / 5);

    if (you.duration[DUR_FIRE_SHIELD] > 50)
        you.duration[DUR_FIRE_SHIELD] = 50;

    mpr("The air around you leaps into flame!");

    manage_fire_shield();
}

void cast_confusing_touch(int power)
{
    msg::stream << "Your " << your_hand(true) << " begin to glow "
                << (you.duration[DUR_CONFUSING_TOUCH] ? "brighter" : "red")
                << "." << std::endl;

    you.duration[DUR_CONFUSING_TOUCH] += 5 + (random2(power) / 5);

    if (you.duration[DUR_CONFUSING_TOUCH] > 50)
        you.duration[DUR_CONFUSING_TOUCH] = 50;

}

bool cast_sure_blade(int power)
{
    bool success = false;

    if (!you.weapon())
        mpr("You aren't wielding a weapon!");
    else if (weapon_skill(you.weapon()->base_type,
                          you.weapon()->sub_type) != SK_SHORT_BLADES)
    {
        mpr("You cannot bond with this weapon.");
    }
    else
    {
        if (!you.duration[DUR_SURE_BLADE])
            mpr("You become one with your weapon.");
        else if (you.duration[DUR_SURE_BLADE] < 25)
            mpr("Your bond becomes stronger.");

        you.duration[DUR_SURE_BLADE] += 8 + (random2(power) / 10);

        if (you.duration[DUR_SURE_BLADE] > 25)
            you.duration[DUR_SURE_BLADE] = 25;

        success = true;
    }

    return (success);
}

void manage_fire_shield()
{
    you.duration[DUR_FIRE_SHIELD]--;

    if (!you.duration[DUR_FIRE_SHIELD])
        return;

    // Place fire clouds all around you
    for ( adjacent_iterator ai; ai; ++ai )
        if (!grid_is_solid(grd(*ai)) && env.cgrid(*ai) == EMPTY_CLOUD)
            place_cloud( CLOUD_FIRE, *ai, 1 + random2(6), KC_YOU );
}
