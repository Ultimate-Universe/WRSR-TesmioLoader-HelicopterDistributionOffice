# Changelog

## 1.1.0

- Fixed existing helicopters being accepted for reassignment to the Helicopter Distribution Office but failing to register as resident office vehicles after arrival.
- Rehomed helicopters now use WRSR's native helicopter residence/parking transition, allowing them to land, appear in the office vehicle list and receive Distribution Office work normally.
- Hardened plugin startup so the new residence hook is validated before the established HDO hooks are applied, avoiding a partial-plugin state if the target cannot be safely hooked.

## 1.0.0

- Initial public release.
