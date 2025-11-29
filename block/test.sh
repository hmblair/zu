#!/bin/zsh

# Simple test script for blk utility
# Run with: ./test.sh

SCRIPT_DIR="${0:A:h}"
BLK="$SCRIPT_DIR/blk"
BLKD="$SCRIPT_DIR/blkd"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

pass() { echo "${GREEN}✓ PASS:${NC} $1"; }
fail() { echo "${RED}✗ FAIL:${NC} $1"; exit 1; }
info() { echo "${YELLOW}► $1${NC}"; }

# ─────────────────────────────────────────────────────────────────────────────
# Syntax checks
# ─────────────────────────────────────────────────────────────────────────────

info "Checking syntax..."

zsh -n "$BLK" 2>/dev/null && pass "blk script syntax" || fail "blk script syntax"
zsh -n "$BLKD" 2>/dev/null && pass "blkd daemon syntax" || fail "blkd daemon syntax"

# ─────────────────────────────────────────────────────────────────────────────
# Help command
# ─────────────────────────────────────────────────────────────────────────────

info "Testing help command..."

output=$("$BLK" help 2>&1)
[[ "$output" == *"Usage: blk"* ]] && pass "help shows usage" || fail "help shows usage"
[[ "$output" == *"add"* ]] && pass "help shows add command" || fail "help shows add command"
[[ "$output" == *"daily"* ]] && pass "help shows daily command" || fail "help shows daily command"
[[ "$output" == *"remove"* ]] && pass "help shows remove command" || fail "help shows remove command"
[[ "$output" == *"list"* ]] && pass "help shows list command" || fail "help shows list command"
[[ "$output" == *"Wildcard"* ]] && pass "help shows wildcard info" || fail "help shows wildcard info"

# ─────────────────────────────────────────────────────────────────────────────
# Argument validation (these fail before password prompt)
# ─────────────────────────────────────────────────────────────────────────────

info "Testing input validation..."

# Test missing arguments (these error immediately, no password prompt)
output=$("$BLK" add 2>&1)
[[ "$output" == *"Usage"* ]] || [[ "$output" == *"Error"* ]] && pass "add requires arguments" || fail "add requires arguments"

output=$("$BLK" add test.com 2>&1)
[[ "$output" == *"Usage"* ]] || [[ "$output" == *"Error"* ]] && pass "add requires duration" || fail "add requires duration"

output=$("$BLK" daily 2>&1)
[[ "$output" == *"Usage"* ]] || [[ "$output" == *"Error"* ]] && pass "daily requires arguments" || fail "daily requires arguments"

output=$("$BLK" daily test.com 2>&1)
[[ "$output" == *"Usage"* ]] || [[ "$output" == *"Error"* ]] && pass "daily requires schedule" || fail "daily requires schedule"

output=$("$BLK" remove 2>&1)
[[ "$output" == *"Usage"* ]] || [[ "$output" == *"Error"* ]] && pass "remove requires arguments" || fail "remove requires arguments"

output=$("$BLK" status 2>&1)
[[ "$output" == *"Usage"* ]] || [[ "$output" == *"Error"* ]] && pass "status requires arguments" || fail "status requires arguments"

# Test invalid duration format (errors before password)
output=$("$BLK" add test.com abc 2>&1)
[[ "$output" == *"Invalid duration"* ]] && pass "rejects invalid duration 'abc'" || fail "rejects invalid duration 'abc'"

output=$("$BLK" add test.com 5x 2>&1)
[[ "$output" == *"Invalid duration"* ]] && pass "rejects invalid duration '5x'" || fail "rejects invalid duration '5x'"

# Test invalid schedule format (errors before password)
output=$("$BLK" daily test.com badschedule 2>&1)
[[ "$output" == *"Invalid schedule"* ]] && pass "rejects invalid schedule 'badschedule'" || fail "rejects invalid schedule 'badschedule'"

output=$("$BLK" daily test.com 25:00-10:00 2>&1)
[[ "$output" == *"Invalid"* ]] && pass "rejects invalid time '25:00'" || fail "rejects invalid time '25:00'"

# ─────────────────────────────────────────────────────────────────────────────
# Unknown command and flags
# ─────────────────────────────────────────────────────────────────────────────

info "Testing error handling..."

output=$("$BLK" foobar 2>&1)
[[ "$output" == *"Unknown command"* ]] && pass "rejects unknown command" || fail "rejects unknown command"

output=$("$BLK" add --badoption test.com 1h 2>&1)
[[ "$output" == *"Unknown option"* ]] && pass "rejects unknown option in add" || fail "rejects unknown option in add"

output=$("$BLK" daily --badoption test.com 09:00-17:00 2>&1)
[[ "$output" == *"Unknown option"* ]] && pass "rejects unknown option in daily" || fail "rejects unknown option in daily"

output=$("$BLK" list --badoption 2>&1)
[[ "$output" == *"Unknown option"* ]] && pass "rejects unknown option in list" || fail "rejects unknown option in list"

# ─────────────────────────────────────────────────────────────────────────────
# Setup check
# ─────────────────────────────────────────────────────────────────────────────

info "Testing setup requirement..."

# Check if setup exists
if [[ -f "$HOME/.blk/salt" ]]; then
    pass "setup exists (skipping setup requirement test)"
else
    # If not set up, commands should error
    output=$("$BLK" list 2>&1 < /dev/null)
    [[ "$output" == *"not been set up"* ]] && pass "list requires setup" || fail "list requires setup"
fi

# ─────────────────────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo "${GREEN}All tests passed!${NC}"
echo ""
echo "Note: These are basic syntax and validation tests."
echo "Full integration testing requires running 'blk setup' first."
