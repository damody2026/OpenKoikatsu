# StreetSocial common content completion

2026-10-07. Primary independently verified Grok-generated exports; no KKS installation is required for runtime voice playback.

## Evidence
- `Saved/StreetSocial/CompletionPortable01`: schema2,1230 source nodes,705 branch objects, personalities0,18,17,27,37. Branch selection metadata retains source containers, index ordering, controlling conditions and individual outcome rows.
- Export completed in8.89s. Japanese slots:851 copied,38 reused,889 source references checked. Missing Japanese/Chinese voices:0; source sequence mismatches:0; Japanese SHA mismatches:0; nonportable voice references:0.
- Japanese OGGs are copied into `Content/IndependentResources/StreetSocialData/ja_ogg`. Adapter resolves their project-relative audio root once and emits absolute runtime references; it never parses KKS bundles inside the game.
- `python -m unittest discover -s Tools/tests -p test_street_social_catalogue.py`:21 tests passed, including exact bilingual sequence alignment and portable references.
- `Saved/StreetSocial/IntegrationCompletion02`: real six-APNG manifest, normalized catalogue in both languages and rules aliases for preference/communication curves.

## Presentation limits
Original pose states are retained as provenance, not falsely mapped to four generic gestures.940 rows have unavailable exact gesture clips;290 have no pose. Named expressions remain source-only and look patterns retain their original numeric IDs. Street presentation uses explicitly labelled common gestures00/03/07/12 and runtime audio-driven mouth motion. Exact named facial expression and gaze implementations remain outside this first demo.

Friendship rank-up and confession can launch ADV scenarios rather than PCM. The first demo previews these outcomes with explicit text and commits only on confirmation. It does not invent a voiced event or claim a complete ADV port. Unknown original world conditions are disabled; missing hotel/prayer/player stats use documented street-neutral policy where selection formulas support it.

## Runtime boundary
Only common content is exported here. Player APNG cards remain runtime inputs; private rig and GPU pose preparation remain background work, with independent cache keys and a shared4GiB limit. No card-specific cooked assets are required.
