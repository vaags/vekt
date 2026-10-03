# Product Naming

Read this before naming a new product, scaffolding a plugin folder, or renaming an existing one.

## Vekt is the framework, not a product

Vekt is the name of the framework only. A product's name is its own word: **Rav**, not "Vekt Rav". This covers every
place the product name appears: the plugin's `PRODUCT_NAME`, its `productName` constant (processor name, preset bank
and preset folder), its folder, CMake targets, the editor title and docs. (Decided by Thomas, 3 October 2026.)

## The maker is Thomas Vaags

The maker is Thomas Vaags, an individual, not a company and not Vekt. Hosts show the maker next to the product
("Thomas Vaags: Rav"). The root `CMakeLists.txt` owns the maker identity and every product uses it (decided by Thomas,
3 October 2026):

| Field | Value |
| --- | --- |
| `COMPANY_NAME` (`VEKT_MAKER_NAME`) | `Thomas Vaags` |
| `PLUGIN_MANUFACTURER_CODE` (`VEKT_MAKER_CODE`) | `Tava` |
| Bundle-ID prefix (`VEKT_BUNDLE_ID_PREFIX`) | `com.thomasvaags`, so `com.thomasvaags.<product>` |
| User preset folder | `~/Library/Audio/Presets/Thomas Vaags/<product>` |

The maker code and each product's `PLUGIN_CODE` identify the plugin to hosts and are frozen at the first release. A
later brand can change the displayed maker name, but not the code. Each product needs its own `PLUGIN_CODE`: Rav
`Ravv`, Glimmer `Glmr`, Mono `Kobr` (already set for the Kobber rename).

The bundle-ID prefix is not part of the host identity (AU uses type, subtype and maker code; the VST3 class ID is
derived from the codes), so changing it does not break saved projects. Published bundle IDs are still immutable
([ARCHITECTURE.md](ARCHITECTURE.md)): `com.thomasvaags` is kept for now, and if a brand with its own domain comes,
switch to that domain only before the first release.

The product identifier saved in presets and project state stays `com.vekt.<product>`: it is a Vekt data-format key,
like `"format": "vekt.preset"`, not a maker field, and no host or user sees it.

## Naming pattern

A product is named after a natural material in Norwegian:

1. **One short word**, one or two syllables, that is easy to say in English.
2. **A physical material** such as a stone, mineral, metal or resin, not an abstract idea or an adjective.
3. **A link to the sound.** Something about the material, such as its look, texture or behaviour, hints at what the
   product does.
4. **Plain ASCII.** Avoid æ, ø and å, which cause trouble in plugin IDs, bundle names, paths and search.
5. **Not already taken** by another audio product. Check before the name reaches a release.

| Product | Norwegian meaning | What it is | Link to the sound |
| --- | --- | --- | --- |
| Rav | amber | fossil resin, golden and translucent | warm, glowing colour, suits saturation and fuzz |
| Glimmer | mica | mineral in thin, glittering sheets | the shimmer and swirl of a rotary speaker |
| Flint | flint | hard stone that makes sparks when struck | sharp strike with a fast attack, suits a drum synth |
| Kobber | copper | conductive metal, wiring and circuits | warm analog synth with three filter circuits |

*Rav* means amber, not ember (ember is *glo*).

## Status of existing products

- **Mono will be renamed Kobber** (decided by Thomas, 3 October 2026; not yet scheduled). Mono is a polyphonic synth
  with three filters, so the old name is also misleading. Rename it only when asked. Its `PLUGIN_CODE` is already
  `Kobr`.
- Product names no longer carry the Vekt prefix (3 October 2026). The folders (`plugins/rav`), CMake targets
  (`Rav_VST3`) and namespaces (`vekt::rav`) still do; drop the folder and target prefix together with the Kobber
  rename, when asked.
- New products start without the prefix; Flint is planned as `plugins/flint`.
- Known risk (3 October 2026): JUCE names the Standalone settings file after the product, at the top of
  `~/Library/Application Support` (`Rav.settings`, `Mono.settings`), so another app with the same file name would
  share it. Resolve it with the Kobber rename, for example with a `Thomas Vaags` subfolder.

Products are pre-release, so a rename does not yet break saved projects. It does change plugin and bundle identifiers,
preset folders, test labels and scripts that name the product, so plan it as its own change.
