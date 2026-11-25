# Stats (MealKit module)

Fixed-point attributes with base values and modifiers recomputed in a fixed order, for [MealKit](https://github.com/MealKit-Engine/MealKit) games. Docs: [docs/index.md](docs/index.md).

```
tools\module.ps1 add -Project <your project> stats
```

Tests (not copied into projects): `cmake -S tests -B build/tests -G Ninja -DMEALKIT_ROOT=<engine> -DMEALKIT_HEADLESS_ONLY=ON`, then build and run `stats_tests`.
To publish: push a `v<version>` tag that matches `version` in `module.txt`, then add `entries/stats.txt` to [MealKit-Modules](https://github.com/MealKit-Engine/MealKit-Modules) in a pull request.

MIT license.
