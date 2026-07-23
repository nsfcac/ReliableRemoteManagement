Let me lay out a plan. First, quick snapshot of where things stand, then a prioritized sequence.

## Where we are

- **Firmware**: dynamic config handshake works, committed on `feature/mqtt_hello`. Contract-alignment fixes still open (#18 mac check, #19 enabled:false, plus dropping `topics{}`). Role-based sender (#12) and config_ack (#13) pending.
- **Contract**: `docs/mqtt-contract.md` mostly filled; §7/§8/§9 TODOs remain (task #1).
- **Host services**: provisioning works; ingestion empty; Docker files done; folder restructure proposed but not applied.
- **Infra**: you now have the switch + USB-Ethernet, and you are migrating Pi to Radxa X4 (x86). Network/DHCP not set up yet.

## The plan, in dependency order

**Phase 0 (now): Radxa network foundation.** This is the physical thing in hand and it unblocks the most.
1. Wire the USB-Ethernet to the switch, ESP32s to the switch.
2. Configure the Radxa telemetry interface: static IP, default route stays on the home side (so it keeps internet/GitHub/SSH).
3. dnsmasq for DHCP on the telemetry subnet.
4. Confirm an ESP32 gets a lease and can reach the broker at the Radxa's telemetry IP.
5. **This settles addressing** (the telemetry subnet + Radxa IP), which the firmware has been waiting on.

**Phase 1: firmware points at the isolated LAN.** Set `HOST_IP` via PlatformIO build flag to the Radxa telemetry IP, reflash, watch hello to config to telemetry on the real segment. First true end-to-end run in the target topology.

**Phase 2: host foundation** (can run in parallel with 0/1, it is pure code).
- Apply the folder restructure (`app/`, `config/`, `setup/`, `docs/`).
- Write `ingestion_service.py`.
- Write the idempotent `setup.sh` for the Radxa (docker / network / dnsmasq), which also codifies Phase 0.

**Phase 3: CD pipeline.** Self-hosted runner on the (now-stable) Radxa, auto-deploy host services on push to main.

**Phase 4: firmware contract-alignment + tests** (ongoing, independent). #18, #19, drop `topics{}`, then #12/#13, then the test coverage tree (#14/#15/#16).

## My recommendation for "now"

Start **Phase 0**. Reasons we already established: it is the hardware in front of you, it settles the addressing every other piece depends on, and it gives you the real environment to test against instead of the home network you are leaving. Everything else risks rework if the addressing is not nailed first.

A nice efficiency: as you do Phase 0 by hand, capture each step into `setup/02-network.sh` and `setup/03-dnsmasq.sh` (Phase 2) so the manual work becomes the reusable script instead of throwaway commands.

## Tracking

There are a few things we discussed but never filed: the **CD pipeline**, the **Radxa setup script**, and the **firmware endpoint build-flag** work. Want me to open those as issues so the plan is fully tracked on GitHub, and refresh the local task list (it is stale, all firmware) to reflect these phases? Then we start on the Phase 0 network setup, with me guiding and you running the commands.