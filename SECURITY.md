# Security policy

Do not attach access tokens, signing keys, full process inventories or personal network logs to public issues. Before this repository is made public, configure GitHub private vulnerability reporting; until then contact the owner through an already established private channel.

The update trust root is the embedded ECDSA P-256 public key (`assets/update-key.bin`). HTTPS and GitHub asset digests alone are not publisher authentication. Both local and online update paths verify the signed manifest, SHA-256, product name, AMD64 architecture and four-part file version. There is no Authenticode certificate in this delivery.

Only configured repository asset URLs and the two documented GitHub asset CDN hostnames are accepted. HTTPS certificate verification is left enabled. Repository access tokens are not accepted or persisted. The updater validates again while controlling file handles; the GUI does not overwrite its own running EXE. Failure recovery is provided by the existing local transaction journal, not a service or scheduled task.

Installed-app metadata is untrusted. `UninstallString` is never executed or used as a launch command. The application picker does not execute selected EXEs. Publisher metadata is not a verified signature. MSIX identities are retained without pretending that their protected package paths implement network constraints.

Tests compile out real adapter modification. Do not run the production program's switches during automated CI. No WFP policy, route, metric, DNS or proxy mutation is added by these features.

Release gating: preserve the offline private key outside the repository, require human review for `release-signing` and `release-publication` environments, keep signing on a dedicated trusted Windows identity, and never run fork/PR code on the signing runner. Signing and publishing workflows default to disabled. Repository tags and source must be reviewed before approval.
