# The options overlay: adding and describing an option

The options overlay is drawn over the paused game, full screen and
semi-transparent, on the Switch and the PC (F10, or Select on a pad). Its
pages, entries and options are data in `port/src/menu/menu.json`; the defaults
are in `port/src/defaults.json`; the model is `port/src/menu/menu_model.*`.

## Where an option's help goes

An option shows a description next to the selected entry. Choose its shape by
what the values are:

- **The values differ in kind: describe each value.** Use `helpBy`, one text
  per option id (`"0"` and `"1"` for a toggle). The description follows the
  value under the cursor. Example: the gamepad mode, where each value is a
  different way to play and the player must read what it does before choosing.
- **The values explain themselves: one text for the option.** Use `help`.
  Examples: the crosshair colour, the anisotropy level, an on/off flag. The
  label already says what each value does; the text says what the option is for.

Do not write a per-value text that only repeats the label ("Red: red
crosshair"), and do not squeeze several different behaviours into one text.
