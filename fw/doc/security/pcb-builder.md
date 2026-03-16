# PCB Builder's Guide to JLPiCart Security

This guide is for people who designed or assembled a JLPiCart board and want to
understand how the security model works in practice — from first power-on through
to shipping a sealed production unit.

Read [overview.md](overview.md) first if you have not already.

---

## Day 0: I just assembled the board

Congratulations. Flash the firmware with `picotool` or by holding BOOTSEL and dragging
the UF2 file. The board will boot in **development mode**.

In development mode, the board functions fully. All API services work. Collections
install. The menu shows. The only differences from a sealed unit are:

- Any firmware can be flashed, including builds you compiled yourself.
- The Device Identity Key (DIK) is generated on first boot and stored in flash, but
  without hardware-rooted encryption. The menu and `GET_SECURITY_INFO` API call will
  show `otp_device_secret_present = false`.
- If the platform ever connects to JLPiCart online services, the server knows this
  is an unprovisioned unit. Leaderboard scores from unprovisioned units are flagged.
- There is no protection against someone reflashing the device with different firmware.

This is the correct mode for development. You should not do OTP writes until you are
confident the firmware you want to run is correct, because some OTP writes are
irreversible.

---

## Understanding what OTP does

The RP2350 has 8KB of one-time-programmable (OTP) memory. "One-time" means each bit
can only be written once — there is no erase. You can add information but you cannot
remove it.

JLPiCart uses OTP for four purposes:

| OTP content | What it does | Reversible? |
|---|---|---|
| Boot key fingerprints (slots 0–3) | RP2350 verifies firmware against these at every boot | Slots can be invalidated (revoked), but not reused |
| Device secret seed | Source of all storage encryption keys | No |
| Security lock flags | Disable SWD/debug, disable USB boot, enable anti-rollback | No |
| Anti-rollback counter | Monotonic counter prevents installing older firmware | No (counter only increases) |

The key insight: **OTP writes are the point of no return.** Do them in the right order,
and only when you are sure you want them.

---

## The provisioning sequence

These steps should be done in this order. Later steps depend on earlier ones. Do not
skip steps unless you understand the consequences (noted below).

### Step 1 — Decide on boot key ownership

Before doing any OTP writes, decide: who controls the boot key for this board?

**Option A: Use only the JLPiCart platform key (Slot 0)**
The platform maintainer holds a Firmware Signing Key whose fingerprint is enrolled in
Slot 0 on all official boards. If you use this option, only officially signed firmware
will run on your sealed board. You get the benefit of official updates but you cannot
run custom firmware on a sealed unit.

**Option B: Enroll your own key (Slot 1)**
Generate an ed25519 keypair offline. Keep the private key in a secure location (an
offline machine, a hardware security module, or at minimum an encrypted drive).
You will enroll the public key fingerprint in OTP Slot 1. From that point, only
firmware signed with your private key (or the platform key in Slot 0, if also enrolled)
will run on this board.

**Option C: Enroll only your own key, revoke Slot 0**
For boards where you want to be the only signer. After enrolling your key in Slot 1,
you can mark Slot 0 as invalid. This is permanent. Only do this if you are certain you
never want to run platform-signed firmware on this unit.

For most builders who want to ship a cartridge with a specific game: **Option A or B**.
For builders who want full independence: **Option B or C**.

For development and prototype units: do nothing yet. Sealed units come later.

### Step 2 — Program the device secret seed

The device secret is a 32-byte random value written to OTP. The firmware reads it at
boot, derives the Storage Master Key (SMK), and uses the SMK to encrypt all user data
in flash.

**To program the device secret:**
```
# Using the JLPiCart provisioning tool (provisioning-tool is planned):
jlpicart-provision write-device-secret --random

# The tool:
# 1. Generates 32 bytes from a secure random source
# 2. Asks you to confirm ("This is irreversible. Proceed? [yes/no]")
# 3. Writes to the OTP device secret region
# 4. Verifies the write succeeded by reading back and comparing
# 5. Saves a backup of the secret (encrypted) for recovery purposes — keep this safe
```

**What happens if you skip this step:**
The board operates in "unprovisioned" mode. All data is stored without hardware-rooted
encryption. The menu and API show `otp_device_secret_present = false`. This is fine for
development. For a shipping cartridge, you should program the device secret.

**What happens if you lose the device secret:**
You cannot decrypt user data on that unit. Saves, profiles, and credentials are
unrecoverable. There is no way to extract the OTP value without invasive silicon
techniques. Keep the backup.

**What happens to existing data when you program the device secret:**
Any data already in flash (saves, profiles) was stored unencrypted (or with a
zero-derived key). After programming the OTP secret, the firmware detects that the
storage was created without a hardware secret and re-encrypts it on first boot. This
migration happens automatically.

### Step 3 — Enroll your boot key fingerprint (if using Option B or C)

Generate an ed25519 keypair. You will use the private key with a signing tool to
sign firmware images. The public key fingerprint (SHA-256 of the public key bytes)
is what goes into OTP.

```
# Generate a keypair offline (on an air-gapped machine if possible):
openssl genpkey -algorithm ed25519 -out my_boot_key.pem
openssl pkey -in my_boot_key.pem -pubout -out my_boot_key_pub.pem

# Compute the fingerprint (first 20 bytes of SHA-256 of the raw public key bytes):
# The provisioning tool handles this:
jlpicart-provision enroll-boot-key --slot 1 --pubkey my_boot_key_pub.pem
```

After this step, to run any firmware on this unit you must sign it:
```
jlpicart-sign --key my_boot_key.pem --input firmware.uf2 --output firmware_signed.uf2
```

Unsigned firmware will be rejected at boot by the RP2350. There is no way around this
without invasive hardware attacks.

**Keep the private key offline and backed up.** If you lose it, you cannot update the
firmware on sealed units enrolled to that key. You would need to use a different enrolled
key slot if one is available.

### Step 4 — Enable secure boot

This OTP write tells the RP2350 to enforce signature verification at every boot. Before
this write, signature verification is performed but boot continues even if it fails.
After this write, unsigned or badly-signed firmware causes the device to halt.

```
jlpicart-provision enable-secure-boot
# "Secure boot will be enforced after next reboot. This is irreversible. Proceed? [yes/no]"
```

**Do not do this step before enrolling at least one boot key.** An OTP with secure
boot enabled but no enrolled keys results in a device that cannot boot any firmware.
Recovery would require BOOTSEL reflash (if USB boot is still enabled) — but if you
also disable USB boot in the next step, the device would be bricked.

**Safe order: enroll key → enable secure boot → optionally disable USB boot.**

### Step 5 — Optional: lock down boot options and debug

These are additional hardening steps. Each is irreversible.

**Disable SWD/debug:**
Prevents an attacker from attaching a debugger to read RAM (including the SMK, which
lives in RAM while the device is running). On a production cartridge, you generally
want this.
```
jlpicart-provision disable-debug
```

**Disable USB boot:**
Prevents using the BOOTSEL button to enter the ROM bootloader and reflash via USB.
After this, firmware can only be updated via the signed firmware update workflow
(the Update service, Stage 25+). Do not do this before you have a working signed
firmware update path, or you will have no way to update the firmware.
```
jlpicart-provision disable-usb-boot
```

**Enable anti-rollback:**
Enables a monotonic counter in OTP. Every signed firmware image carries a version
number. Once a unit has booted firmware with version N, it will refuse to boot
firmware with version < N. This prevents downgrade attacks. The counter only ever
increases.
```
jlpicart-provision enable-anti-rollback
```

### Step 6 — Write the initial policy document

Policy flags control what the firmware permits at runtime: which collection sources
are allowed, whether publisher signatures are required, whether the stable device ID
can be exposed, etc. The policy document is stored in flash and is authenticated
(HMAC-verified by the firmware at every boot).

The default policy (applied when no policy document is present) is conservative:
- USB installs: allowed
- Network installs: allowed
- Unsigned collections: allowed
- Publisher signature required: no
- Expose stable device ID: yes

For a production unit where you want to restrict to signed-only content:
```
# Edit the policy file:
cat > my_policy.json << 'EOF'
{
  "allow_usb_collection_install": true,
  "allow_network_collection_install": true,
  "allow_unsigned_collections": false,
  "allow_user_replace_collections": true,
  "require_publisher_signature": true,
  "allow_boot_key_enrollment": false,
  "allow_boot_key_revocation": false,
  "expose_stable_device_id": false
}
EOF

# Sign and install:
jlpicart-provision write-policy --policy my_policy.json --key my_boot_key.pem
```

Policy updates can be applied in the field via the signed firmware update workflow.
They do not require OTP writes.

---

## Recovery matrix

What breaks and what can be fixed:

| Situation | Recovery |
|---|---|
| Bad firmware (soft brick) | If USB boot is still enabled: hold BOOTSEL, reflash. If USB boot is disabled: use Update service via signed firmware update bundle. |
| Lost private signing key | Cannot sign new firmware. Use a different enrolled key slot if available. Otherwise units are permanently stuck on the current firmware version. |
| Lost OTP device secret backup | Cannot decrypt user data on affected units. Saves and profiles are unrecoverable. Can still boot and run; new data will be encrypted under the new (unchanged) OTP. |
| Enrolled key slot compromised | Revoke the slot (OTP write marking it invalid). Units will refuse firmware signed by the revoked key on next boot. Roll out new firmware signed by a remaining valid key before revoking. |
| All OTP key slots revoked accidentally | Device cannot boot any firmware. Unrecoverable without invasive hardware attack. |

---

## Production checklist

Work through this before shipping any unit:

- [ ] Device secret programmed in OTP (`otp_device_secret_present = true` in GET_SECURITY_INFO)
- [ ] At least one boot key enrolled in OTP
- [ ] Secure boot enabled (`secure_boot_enabled = true`)
- [ ] Firmware builds signed and verified on a test unit before flashing production batch
- [ ] Policy document written and authenticated
- [ ] `allow_unsigned_collections` set according to your distribution model
- [ ] `require_publisher_signature` set according to your distribution model
- [ ] SWD/debug disabled (if shipping sealed units)
- [ ] USB boot disabled only if signed firmware update workflow is tested and working
- [ ] Boot key private key backed up securely (offline, multiple copies)
- [ ] Device secret backup stored securely (offline, multiple copies)
- [ ] Recovery procedure documented and tested on a spare unit
- [ ] Menu shows "Sealed unit — secure boot active" on the security status screen
- [ ] GET_SECURITY_INFO API returns expected posture flags

---

## Frequently asked questions

**Can I have a development unit and a production unit with the same firmware binary?**
Yes. The firmware binary is the same. The difference is the OTP configuration of the
unit. The firmware detects the security posture at boot and adapts its behavior.

**Do I need to use the JLPiCart platform signing key, or can I use my own?**
You can use your own key. Enroll it in Slot 1. You do not need to enroll Slot 0
(the platform key) at all. This gives you complete independence.

**What if I want to allow users to install their own collections on a production unit?**
Set `allow_unsigned_collections: true` and `allow_user_replace_collections: true` in
policy. Users can then install any collection they have on USB. If you also want them
limited to your signed content only, set `require_publisher_signature: true` and
distribute your PIC so users' cartridges trust it.

**What is the difference between the Firmware Signing Key and the Content Signing Key?**
The FSK signs firmware images and is verified by the RP2350 boot ROM against OTP
fingerprints. The CSK signs collection bundles and is verified by the firmware against
a Publisher Identity Certificate. They must be different keys — a CSK must not be able
to sign firmware.

**What happens if I ship a unit without programming the OTP device secret?**
It works fine, but user data (saves, profiles, credentials) is stored without
hardware-rooted encryption. If someone removes the flash chip, they can read the data.
For cartridges that store sensitive user data, program the device secret.

**Can I build a board without the ESP32 (no WiFi)?**
Yes. The board descriptor declares `net.wifi` as a hardware capability that requires
safe probing. If no ESP32 is present, the probe fails and the capability is not
activated. Online features (leaderboards, profile sync) will not be available, but
all local features work.
