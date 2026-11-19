# Memo: condition value operators

Weighed 2026-11-19 and not adopted; nothing below is current behaviour. A row with values
(Location type, Weather, Swimming) or forms (Specific Location, Spell Effect) holds when any
selected value holds (`is any of` / `is none of`); rows are AND-ed, and the same kind may appear
in several rows (docs/custom-situations.md). Revisit once Spell Effect has seen use: it is the
one kind where these ideas have a real case.

## Exclusive groups in Weather's dropdown

Ticking Rain would untick Snow, and a region the other regions.

- Values in a row are OR-ed, so two values that never hold together never conflict: `Rain,
  Snow` reads "in rain or snow".
- It removes rows that are useful today: Rain or Snow (a hood in bad weather); Cool or Cold
  region ("not Warm region" is no substitute, it holds indoors too). Rows are AND-ed, so two
  rows cannot stand in; it would take two situations with the same outfit.
- A saved `["rain", "snow"]` row would need a rule on load.
- Swimming is a single choice for another reason: Swimming lies within In water.

## `is all of`

A third operator: the row holds when every selected value holds.

- It adds nothing that cannot be written: two rows of the same kind are AND-ed already
  (`The weather is Rain` + `The weather is Cold region` = rain in a cold region).
- Most combinations never hold:
  - Weather: only one of Rain / Snow with one region; Rain + Snow and two regions never.
  - Location type: Town + Player home at most.
  - Specific Location: a place and one inside it is the inner place alone; two apart never.
  - Spell Effect: the one real case, two effects on at once; two rows do it too.
- Cost: `Row` gains an any / all flag and situations.json a key; `RowHolds`, the Applies line,
  `Detail::Implies` (Never worn, Always overrides) each need the all case; a warning for a row
  that can never hold, or silence; whether `is not all of` is offered; a three-way operator
  dropdown.

## Weather and Climate as two kinds

Rain / Snow (the sky now, outdoors only) and the regions (the climate, whatever the weather)
as separate condition kinds, so a row of each is AND-ed and Rain + Cold region cannot be misread
as "rain in a cold region".

- The Cold preset (`Snow, Cold region`, `SituationTable::PresetRules`) could no longer be one
  situation.
- situations.json's `weather` rows would need splitting on load.
- Taken instead: a rule between the two groups in Weather's dropdown (`Value::ruleAbove`).
