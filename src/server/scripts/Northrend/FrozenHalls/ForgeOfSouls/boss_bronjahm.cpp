/*
 * Copyright (C) 2008-2015 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "CreatureTextMgr.h"
#include "forge_of_souls.h"

#define SOUND_DEATH        16598
#define DATA_SOUL_POWER    1

enum Events
{
    EVENT_MAGIC_BANE      = 1,
    EVENT_SHADOW_BOLT     = 2,
    EVENT_CORRUPT_SOUL    = 3,
    EVENT_SOULSTORM       = 4,
    EVENT_FEAR            = 5
};

enum Spells
{
    SPELL_MAGIC_S_BANE          = 68793,
    SPELL_SHADOW_BOLT           = 70043,
    SPELL_CORRUPT_SOUL          = 68839,
    SPELL_CONSUME_SOUL          = 68861,
    SPELL_TELEPORT              = 68988,
    SPELL_FEAR                  = 68950,
    SPELL_SOULSTORM             = 68872,
    SPELL_SOULSTORM_CHANNEL     = 69008,    // pre-fight
    SPELL_SOULSTORM_VISUAL      = 68870,    // pre-cast Soulstorm
    SPELL_PURPLE_BANISH_VISUAL  = 68862     // Used by Soul Fragment (Aura)
};

enum Text
{
    SAY_AGGRO           = 0,
    SAY_KILL            = 1,
    SAY_SOUL_STORM      = 2,
    SAY_CORRUPT_SOUL    = 3
};

enum Phases
{
    PHASE_1 = 1,
    PHASE_2 = 2
};

class boss_bronjahm : public CreatureScript
{
    public:
        boss_bronjahm() : CreatureScript("boss_bronjahm") { }

        struct boss_bronjahmAI : public BossAI
        {
            boss_bronjahmAI(Creature* creature) : BossAI(creature, DATA_BRONJAHM)
            {
                DoCast(me, SPELL_SOULSTORM_CHANNEL, true);
				_soulFragmentsSpawned = 0;
            }

            void Reset() override
            {
			    _Reset();
                events.Reset();
                events.SetPhase(PHASE_1);
                events.ScheduleEvent(EVENT_SHADOW_BOLT, 2000);
                events.ScheduleEvent(EVENT_MAGIC_BANE, urand(8000, 20000), 0, PHASE_1);
                events.ScheduleEvent(EVENT_CORRUPT_SOUL, urand(25000, 35000), 0, PHASE_1);
				_soulFragmentsSpawned = 0;
            }

            void EnterCombat(Unit* /*who*/) override
            {
                _EnterCombat();
                me->RemoveAurasDueToSpell(SPELL_SOULSTORM_CHANNEL);
				Talk(SAY_AGGRO);
            }

            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();
                sCreatureTextMgr->SendSound(me, SOUND_DEATH, CHAT_MSG_MONSTER_YELL, 0, TEXT_RANGE_NORMAL, TEAM_OTHER, false);
            }

			void JustReachedHome() override
            {
               _JustReachedHome();
               DoCast(me, SPELL_SOULSTORM_CHANNEL, true);
            }

            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_KILL);
            }

            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/) override
            {
                if (events.IsInPhase(PHASE_1) && !HealthAbovePct(30))
                {
                    events.SetPhase(PHASE_2);
                    DoCast(me, SPELL_TELEPORT);
                    events.ScheduleEvent(EVENT_FEAR, 6000, 0, PHASE_2);
                    events.ScheduleEvent(EVENT_SOULSTORM, 100, 0, PHASE_2);
                }
            }

            void JustSummoned(Creature* summon) override
            {
                summons.Summon(summon);
                summon->SetReactState(REACT_PASSIVE);
                summon->GetMotionMaster()->Clear();
                summon->GetMotionMaster()->MoveFollow(me, me->GetObjectSize(), 0.0f);
                summon->CastSpell(summon, SPELL_PURPLE_BANISH_VISUAL, true);
				++_soulFragmentsSpawned;
            }

            void SummonedCreatureDies(Creature* /*summon*/, Unit* /*killer*/) override
            {
                --_soulFragmentsSpawned; // He can just only spawn the Fragment, so don't check if entry is the fragment
            }

            uint32 GetData(uint32 type) const override
            {
                if (type == DATA_SOUL_POWER)
                    return _soulFragmentsSpawned;
                return 0;
            }

            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_MAGIC_BANE:
                            DoCastVictim(SPELL_MAGIC_S_BANE);
                            events.ScheduleEvent(EVENT_MAGIC_BANE, urand(8000, 20000), 0, PHASE_1);
                            break;
                        case EVENT_SHADOW_BOLT: // While Bronjahm is within the Soulstorm he hurls bolts of dark magic at a random player, inflicting Shadow damage.
                        {
                            if (events.IsInPhase(PHASE_1))
                            {
                                if (!me->IsWithinMeleeRange(me->GetVictim()))
                                    DoCastVictim(SPELL_SHADOW_BOLT);
                            }
                            else if (events.IsInPhase(PHASE_2))
                            {
                                if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true))
                                DoCast(target, SPELL_SHADOW_BOLT);
                            }
                            events.ScheduleEvent(EVENT_SHADOW_BOLT, 2000);
                            break;
                        }							
                        case EVENT_CORRUPT_SOUL:
                            if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 1, 0.0f, true))
                            {
                                Talk(SAY_CORRUPT_SOUL);
                                DoCast(target, SPELL_CORRUPT_SOUL);
                            }
                            events.ScheduleEvent(EVENT_CORRUPT_SOUL, urand(25000, 35000), 0, PHASE_1);
                            break;
                        case EVENT_SOULSTORM:
                            Talk(SAY_SOUL_STORM);
                            me->CastSpell(me, SPELL_SOULSTORM_VISUAL, true);
                            me->CastSpell(me, SPELL_SOULSTORM, false);
                            break;
                        case EVENT_FEAR:
                            if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true)) // Doesn't he fear just one enemy?
                            DoCast(target, SPELL_FEAR);
                            events.ScheduleEvent(EVENT_FEAR, urand(8000, 12000), 0, PHASE_2);
                            break;
                        default:
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }
			private:
            uint32 _soulFragmentsSpawned;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetInstanceAI<boss_bronjahmAI>(creature, FoSScriptName);
        }
};

class npc_corrupted_soul_fragment : public CreatureScript
{
    public:
        npc_corrupted_soul_fragment() : CreatureScript("npc_corrupted_soul_fragment") { }

        struct npc_corrupted_soul_fragmentAI : public ScriptedAI
        {
            npc_corrupted_soul_fragmentAI(Creature* creature) : ScriptedAI(creature)
            {
                instance = me->GetInstanceScript();
            }

            void MovementInform(uint32 type, uint32 id) override
            {
                if (type != FOLLOW_MOTION_TYPE)
                    return;

                if (TempSummon* summ = me->ToTempSummon())
                {
                    ObjectGuid BronjahmGUID(instance->GetGuidData(DATA_BRONJAHM));
                    if (BronjahmGUID.GetCounter() != id)
                        return;

                    if (Creature* bronjahm = ObjectAccessor::GetCreature(*me, BronjahmGUID))
                    {
                        if (me->GetExactDist2d(bronjahm) <= 2.0f)
                            me->CastSpell(bronjahm, SPELL_CONSUME_SOUL, true);
                    }
                    summ->UnSummon();
                }
            }

        private:
            InstanceScript* instance;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetInstanceAI<npc_corrupted_soul_fragmentAI>(creature);
        }
};

class spell_bronjahm_magic_bane : public SpellScriptLoader
{
    public:
        spell_bronjahm_magic_bane() :  SpellScriptLoader("spell_bronjahm_magic_bane") { }

        class spell_bronjahm_magic_bane_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_bronjahm_magic_bane_SpellScript);

            void RecalculateDamage()
            {
                if (GetHitUnit()->getPowerType() != POWER_MANA)
                    return;

                const int32 maxDamage = GetCaster()->GetMap()->GetSpawnMode() == 1 ? 15000 : 10000;
                int32 newDamage = GetHitDamage();
                newDamage += GetHitUnit()->GetMaxPower(POWER_MANA)/2;
                newDamage = std::min<int32>(maxDamage, newDamage);

                SetHitDamage(newDamage);
            }

            void Register() override
            {
                OnHit += SpellHitFn(spell_bronjahm_magic_bane_SpellScript::RecalculateDamage);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_bronjahm_magic_bane_SpellScript();
        }
};

class spell_bronjahm_consume_soul : public SpellScriptLoader
{
    public:
        spell_bronjahm_consume_soul() : SpellScriptLoader("spell_bronjahm_consume_soul") { }

        class spell_bronjahm_consume_soul_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_bronjahm_consume_soul_SpellScript);

            void HandleScript(SpellEffIndex effIndex)
            {
                PreventHitDefaultEffect(effIndex);
                GetHitUnit()->CastSpell(GetHitUnit(), GetEffectValue(), true);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_bronjahm_consume_soul_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_bronjahm_consume_soul_SpellScript();
        }
};

class spell_bronjahm_soulstorm_channel : public SpellScriptLoader
{
    public:
        spell_bronjahm_soulstorm_channel() : SpellScriptLoader("spell_bronjahm_soulstorm_channel") { }

        class spell_bronjahm_soulstorm_channel_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_bronjahm_soulstorm_channel_AuraScript);

            void HandlePeriodicTick(AuraEffect const* /*aurEff*/)
            {
                PreventDefaultAction();
                for (uint32 i = 68904; i <= 68907; ++i)
                    GetTarget()->CastSpell(GetTarget(), i, true);
            }

            void Register() override
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_bronjahm_soulstorm_channel_AuraScript::HandlePeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_bronjahm_soulstorm_channel_AuraScript();
        }
};

class spell_bronjahm_soulstorm_visual : public SpellScriptLoader
{
    public:
        spell_bronjahm_soulstorm_visual() : SpellScriptLoader("spell_bronjahm_soulstorm_visual") { }

        class spell_bronjahm_soulstorm_visual_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_bronjahm_soulstorm_visual_AuraScript);

            void HandlePeriodicTick(AuraEffect const* aurEff)
            {
                PreventDefaultAction();
                if (aurEff->GetTickNumber()%5)
                    return;
                GetTarget()->CastSpell(GetTarget(), 68886, true);
                for (uint32 i = 68896; i <= 68898; ++i)
                    GetTarget()->CastSpell(GetTarget(), i, true);
            }

            void Register() override
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_bronjahm_soulstorm_visual_AuraScript::HandlePeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_bronjahm_soulstorm_visual_AuraScript();
        }
};

class DistanceCheck
{
    public:
        explicit DistanceCheck(Unit* _caster) : caster(_caster) { }

        bool operator() (WorldObject* unit) const
        {
            if (caster->GetExactDist2d(unit) <= 10.0f)
                return true;
            return false;
        }

        Unit* caster;
};

class spell_bronjahm_soulstorm_targeting : public SpellScriptLoader
{
    public:
        spell_bronjahm_soulstorm_targeting() : SpellScriptLoader("spell_bronjahm_soulstorm_targeting") { }

        class spell_bronjahm_soulstorm_targeting_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_bronjahm_soulstorm_targeting_SpellScript);

            void FilterTargetsInitial(std::list<WorldObject*>& targets)
            {
                targets.remove_if(DistanceCheck(GetCaster()));
                sharedTargets = targets;
            }

            // use the same target for first and second effect
            void FilterTargetsSubsequent(std::list<WorldObject*>& targets)
            {
                targets = sharedTargets;
            }

            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_bronjahm_soulstorm_targeting_SpellScript::FilterTargetsInitial, EFFECT_1, TARGET_UNIT_DEST_AREA_ENEMY);
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_bronjahm_soulstorm_targeting_SpellScript::FilterTargetsSubsequent, EFFECT_2, TARGET_UNIT_DEST_AREA_ENEMY);
            }

            std::list<WorldObject*> sharedTargets;
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_bronjahm_soulstorm_targeting_SpellScript();
        }
};

class achievement_bronjahm_soul_power : public AchievementCriteriaScript
{
public:
    achievement_bronjahm_soul_power() : AchievementCriteriaScript("achievement_bronjahm_soul_power") { }

    bool OnCheck(Player* /*source*/, Unit* target) override
    {
        if (!target)
            return false;
        return target->GetAI()->GetData(DATA_SOUL_POWER) >= 4;
    }
};

void AddSC_boss_bronjahm()
{
    new boss_bronjahm();
    new npc_corrupted_soul_fragment();
    new spell_bronjahm_magic_bane();
    new spell_bronjahm_consume_soul();
    new spell_bronjahm_soulstorm_channel();
    new spell_bronjahm_soulstorm_visual();
    new spell_bronjahm_soulstorm_targeting();
	new achievement_bronjahm_soul_power();
}