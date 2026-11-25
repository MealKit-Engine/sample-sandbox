# Game AI (MealKit module)

Utility scoring, an enum state machine, sight and hearing, and nav grid route following for [MealKit](https://github.com/MealKit-Engine/MealKit) games. Docs: [docs/index.md](docs/index.md).

```
tools\module.ps1 add -Project <your project> ai
```

Tests (not copied into projects): `cmake -S tests -B build/tests -G Ninja -DMEALKIT_ROOT=<engine> -DMEALKIT_HEADLESS_ONLY=ON`, then build and run `ai_tests`.
To publish: push a `v<version>` tag that matches `version` in `module.txt`, then add `entries/ai.txt` to [MealKit-Modules](https://github.com/MealKit-Engine/MealKit-Modules) in a pull request.

MIT license.
