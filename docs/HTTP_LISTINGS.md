# Large HTTP directory listings

Directory browsing streams a listing GET through a bounded HTML parser. It
does not buffer the whole response or issue a HEAD for each child. Explicit
stat/Properties still obtain authoritative metadata independently.

## Listing metadata

Plain decimal integers, optionally followed by `B`, are exact bytes, including
zero and `UINT64_MAX`. K/KB/KiB, M/MB/MiB, G/GB/GiB and T/TB/TiB accept an
optional decimal fraction and whitespace before the unit, case-insensitively.
Autoindex unit spellings use binary multipliers (1024 per level), consistent
with Navi8or's existing display units. Fractions are truncated to whole bytes;
scaled values are always marked approximate, even if they happen to multiply
to an integer. Signs, exponents, unknown suffixes, malformed fractions and
values outside `uint64_t` are rejected rather than wrapped.

`NAV_ENTRY_SIZE_KNOWN` permits display, sorting and summaries;
`NAV_ENTRY_SIZE_APPROXIMATE` additionally identifies rounded listing metadata.
Panels show clean sizes, for example, `14 KB` or `2.5 MB`; Properties retains
the `~` approximation marker. Byte-mode values too wide for the size column
fall back to human-readable units rather than clipping significant digits.
Summaries indicate rounded,
incomplete or saturated totals with `~`. Directories and unavailable `-` sizes
do not acquire a known zero-byte size.

Both `14-Sep-2026 18:45` and `2026-09-14 18:45` dates are supported alongside
sizes in preformatted and simple table rows, including metadata split across
transport callbacks. Generic `<a href>` indexes remain best-effort: missing
metadata stays unknown. Size fields ignore trailing descriptions or MIME
types. Table-cell boundaries and whitespace entities are retained/normalized
by the streaming parser, including multiline rows, without building a DOM.
JavaScript-generated listings, sizes only in attributes
or anchor labels, unrelated date layouts and arbitrary description columns are
not interpreted as authoritative size/date metadata.

## Progress and cancellation

The optional provider `list_progress` operation accepts `NavListOptions` with
progress and cancellation callbacks plus userdata. `NavListProgress` carries
received bytes, an optional total, and parsed unique children (excluding the
synthetic parent). Providers without this optional operation keep their normal
list operation; the pane boundary can report their final child count with an
unknown byte total. Providers never call UI functions.

HTTP reports parsed entries after body callbacks and total/cancellation updates
through libcurl's XFERINFO callback. Repository-open UI paints the status at
most ten times per second and polls the terminal for a configured Dialog Cancel
command at most twenty times per second. This is synchronous, not a background
listing or a new provider event loop. Unknown/chunked lengths work normally:

```text
Loading repository... 26.8 MB   18742 entries   Esc Cancel
Loading repository... 26.8 MB / 52.7 MB   18742 entries   Esc Cancel
```

Cancellation aborts curl and frees the pending listing. Failures and cancellation
leave the old pane, selection, location and history intact. Success replaces it
transactionally and shows `Repository opened`. Large listings briefly show
`Sorting N entries...`; an in-place heapsort bounds sorting to O(n log n),
replacing the old quadratic insertion sort without allocating another entry
array.

## Memory scaling and follow-up

On 64-bit Linux, `sizeof(NavEntry)` is 4376 bytes: the fixed 4096-byte
`resource_id` alone accounts for about 94%. The array doubles its capacity.
The following figures exclude hash-index storage, allocator overhead, the old
pane retained during loading and possible transient reallocation copies.

| Children | Entry payload | Allocated capacity including parent | Array allocation | IDs in payload |
| --- | ---: | ---: | ---: | ---: |
| 30,000 | 125.2 MiB | 32,768 | 136.75 MiB | 117.2 MiB |
| 100,000 | 417.3 MiB | 131,072 | 547 MiB | 390.6 MiB |

A follow-up should store each repository's common base once and put variable
length names/provider-relative identities in a listing-owned arena or string
pool. Preserve opaque provider identity semantics, stable ownership across
sort/reload, and materialize full identities at the provider boundary. This
change deliberately leaves `NavEntry` storage and provider identity contracts
unchanged.
