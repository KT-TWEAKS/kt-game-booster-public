import test from 'node:test';
import assert from 'node:assert/strict';
import { nextFocusMode, resolveFocusMode } from './focus-mode.js';

test('mantém o modo foco desligado por padrão', () => {
  assert.equal(resolveFocusMode(null), false);
  assert.equal(resolveFocusMode('false'), false);
});

test('interpreta a preferência salva e alterna o estado', () => {
  assert.equal(resolveFocusMode('true'), true);
  assert.equal(nextFocusMode(false), true);
  assert.equal(nextFocusMode(true), false);
});
