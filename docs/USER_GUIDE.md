# eClock User Guide

> You'll never be late for church again.

The eClock is a small, low-power ePaper clock that does one thing brilliantly: it
shows you the time, clearly, at a glance — and keeps running for months on a single
small battery. It never needs winding, and it even corrects itself for daylight
saving.

This guide covers everything you need to use one day-to-day: what the screen is
telling you, what the little icons mean, and how to use the buttons. Every screen
shown here is a real render of what the clock displays.

---

## The screen at a glance

The display always shows one of four screens. Here's what each one looks like and
what it means.

### The clock face (the one you'll see every day)

![The clock face](screenshots/running.png)

| Where | What it is |
| --- | --- |
| **Top left** | Today's date |
| **Top right** | Battery charge (see "Battery" below) |
| **Middle** | The time — big, bold, and readable from across the room |
| **Bottom left** | Sync status + the time it last synced (see "Icons" below) |
| **Bottom right** | AM or PM |

### The syncing screen (first few seconds after power-on)

![The syncing screen](screenshots/syncing.png)

The clock is waiting for a Home Assistant device to send it the correct time. The
address at the bottom right is its Bluetooth address — used once, when you first set
up the sync. If you have Home Assistant, it will lock on almost immediately; the
clock face appears as soon as it has a valid time.

### The "No Time!" screen (only if it can't sync)

![The "No Time!" screen](screenshots/no_time.png)

The clock booted but nobody sent it the time within a few seconds. Don't worry —
just press any button and it will try again. This screen rarely appears once setup is
complete.

### The sleep screen (overnight)

![The sleep screen](screenshots/sleeping.png)

Between **11pm and 5am** the clock turns its display off to save battery and shows a
big **"Zzz"** with the word *Sleeping* beneath it. It wakes itself at 5am, fully
synced and ready for the day. Pressing a button during sleep wakes it immediately
(see "Buttons" below).

---

## The icons, translated

The small symbols on the clock face are your status panel. Here's every one of them,
described as you actually see them on the panel.

### Battery (top right)

| What you see | Meaning |
| --- | --- |
| **A battery icon with a number, e.g. `85%`** | Running on battery. The number is roughly how much charge is left. |
| **A lightning-bolt icon** | Plugged into USB / charger. It's charging — no percentage shown because it's running off the wire, not the cell. |

When the battery gets genuinely low the clock shows it in the percentage — charge it
soon. Because the panel barely uses power, a full charge lasts months.

### Sync status (bottom left)

Next to the icon is a small two-digit time (e.g. `09:41`) — that's the time of the
**last successful sync**, so you always know how fresh your clock's time is.

| What you see | Meaning |
| --- | --- |
| **A white circle with a tick / check mark** | Sync is good. The clock has the correct, up-to-date time. This is the normal "happy" state — you'll see this almost all the time. |
| **A pair of circular arrows** | The clock is syncing right now — it's talking to Home Assistant for the correct time. |
| **A circle with an X / cross** | The last sync failed. The clock still shows the time it has, but it couldn't reach Home Assistant for an update. It'll retry on its own. |

---

## Using the buttons

There are three buttons on the board. For everyday use, they all do the same thing:
**press any of them to tell the clock "you're here" and give it a nudge.**

| Situation | What the button does |
| --- | --- |
| Screen shows "No Time!" | Immediately retries the sync. |
| Middle of the day | Hands-on check — keeps the display fresh. |
| During sleep (night) | Wakes the display up and **keeps it on until the next 11pm**, so it won't go back to sleep if you're up late. Press it again and it stays awake for you. |

That's it. There's nothing to program and nothing to fiddle with — the clock handles
itself, and the buttons are there for the rare moment you need to nudge it.

---

## Everyday use in three sentences

1. It shows the time. Big and clear.
2. It updates itself from Home Assistant, so it's always right — even through daylight
   saving.
3. It sleeps at night to save power and wakes itself at 5am.

Just glance at it on your way out the door. You'll be on time.

---

## First-time setup (for the person building it)

> Hand this part to whoever built your clock — it's the one-time configuration they
> do for you.

The clock gets its time from Home Assistant over Bluetooth. To pair it:

1. Add the custom component from `integrations/homeassistant/` to your Home Assistant.
2. Note the clock's Bluetooth address — it's shown on the syncing screen (bottom
   right) the first time you power it on.
3. Configure the component with that address. It will advertise the clock's presence
   and write the current time to it automatically.
4. The clock switches to the clock face the moment it receives a valid time.

Until a device is paired, the clock simply keeps asking for a sync — it won't run
ahead or drift, it just waits patiently.

---

## Caring for the display

- **The screen keeps its image without power** — that's what makes it so frugal. You
  can pull the battery and the last time stays put.
- **Avoid pressing hard on the panel.** It's a glass display; a gentle touch is all it
  needs.
- **Charge it with a standard USB cable.** You'll rarely need to, but the port is
  handy when you do.

---

## Troubleshooting

| Symptom | What it means | What to do |
| --- | --- | --- |
| Shows "No Time!" | Home Assistant hasn't sent a time yet | Press any button to retry; check HA pairing. |
| Cross icon (sync failed) | It couldn't reach HA for an update | Make sure HA is on and within Bluetooth range; it retries automatically. |
| Bolt icon, no % | It's plugged in / charging | That's normal — it's on the charger. |
| "Zzz" at night | It's sleeping | Normal — it wakes itself at 5am (or on a button press). |

If the clock ever seems confused, give it a quick reset: unplug and replug power, and
it will re-sync all by itself.
