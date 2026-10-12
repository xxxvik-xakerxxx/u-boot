# Native modem ON-adoption handoff

Experimental, default-inactive DT publication, not modem READY or SIM support.
The ordinary metadata profile is unchanged. No hardware write, power request,
SMC, memory-permission change or Linux MD consumer activation is added here.

The successful BROM path leaves the modem physically ON and requires both the
MD consumer and native power supplier disabled in the input DT. Enabling them
before BROM invalidates that input contract. A paired diagnostic kernel can
instead adopt the observed ON state in its real genpd after the final producer.

The explicit empty `/chosen/tetris,modem-native-on-adoption-diagnostic` marker
requests this final-DT behavior. The producer requires one disabled
`mediatek,mt6878-modem-power-controller`, one disabled domain0 and the normal
sole disabled MD metadata consumer. Duplicate, active, malformed or inherited
adoption records are refused before allocation. No marker means no activation.

Only after the real loader owner encodes fresh successful SMEM evidence and
CCCI tags into owned no-map storage does the unpublished final clone gain the
supplier's empty `nothing,modem-inherited-on-adoption` marker and supplier/domain
status `okay`. The MD consumer stays disabled. Cache cleaning and final-DT
validation precede the single commit; errors retain the original disabled DT.
No partial mutation or retry is used as a fallback.

CI exercises the actual producer with unchanged/default inputs, successful
opt-in, nine invalid ownership cases and faults at each activation write.
Source-only checks run locally; all native/U-Boot builds run in GitHub CI.
The paired Linux observation/attach implementation and its DT are staging
work. Do not advertise or install this path as operational radio support.
Native first shutdown, inhibited restart, HIF transport, SIM service and
cellular audio still require implementation and hardware validation.
