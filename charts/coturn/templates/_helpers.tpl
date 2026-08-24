{{- define "coturn.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create chart name and version as used by the chart label.
*/}}
{{- define "coturn.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels
*/}}
{{- define "coturn.labels" -}}
helm.sh/chart: {{ include "coturn.chart" . }}
{{ include "coturn.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Values.image.tag | default .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Create a default fully qualified app name.
We truncate at 63 chars because some Kubernetes name fields are limited to this (by the DNS naming spec).
If release name contains chart name it will be used as a full name.
*/}}
{{- define "coturn.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{- define "coturn.selectorLabels" -}}
app.kubernetes.io/name: {{ include "coturn.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/* Allow KubeVersion to be overridden. */}}
{{- define "kubeVersion" -}}
  {{- default .Capabilities.KubeVersion.Version .Values.kubeVersionOverride -}}
{{- end -}}

{{- define "includeSecurityContext" -}}
  {{- (semverCompare ">= 1.24-0" (include "kubeVersion" .)) -}}
{{- end -}}

{{/*
Effective IP addresses for coturn's listeners. These are the single source of
truth: both the rendered config (configmap-coturn-conf-template.yaml) and the
__COTURN_EXT_IP__ placeholder detection below must use them so the two cannot
drift apart. listening-ip and relay-ip default to the discovered external IP;
federation-listening-ip inherits the effective listening-ip so a host-bound TURN
listener also host-binds federation unless coturnFederationListeningIP overrides.
*/}}
{{- define "coturn.listenIp" -}}
{{- default "__COTURN_EXT_IP__" .Values.coturnTurnListenIP -}}
{{- end -}}
{{- define "coturn.relayIp" -}}
{{- default "__COTURN_EXT_IP__" .Values.coturnTurnRelayIP -}}
{{- end -}}
{{- define "coturn.federationListenIp" -}}
{{- default (include "coturn.listenIp" .) .Values.coturnFederationListeningIP -}}
{{- end -}}

{{/*
Whether the rendered coturn config references the __COTURN_EXT_IP__ placeholder,
i.e. depends on the node's discovered external IP. This drives whether the
get-external-ip helper (and its node-read RBAC) is included. True when
listening-ip/relay-ip (or, with federation enabled, federation-listening-ip)
fall back to the placeholder because their override is unset, or when
external-ip embeds it (including the PUBLIC/PRIVATE form). Returns "true" or "".
*/}}
{{- define "coturn.usesExternalIpPlaceholder" -}}
{{- $ph := "__COTURN_EXT_IP__" -}}
{{/* listening-ip and relay-ip fall back to the placeholder when unset. */}}
{{- $vals := list (include "coturn.listenIp" .) (include "coturn.relayIp" .) -}}
{{/* external-ip only renders when set; it may embed the placeholder in the
     public/private form, e.g. __COTURN_EXT_IP__/__COTURN_HOST_IP__. */}}
{{- if .Values.coturnTurnExternalIP -}}
{{- $vals = append $vals .Values.coturnTurnExternalIP -}}
{{- end -}}
{{/* prometheus-ip defaults to __COTURN_POD_IP__, so it only references the
     external placeholder when explicitly set to it. */}}
{{- if .Values.coturnPrometheusIP -}}
{{- $vals = append $vals .Values.coturnPrometheusIP -}}
{{- end -}}
{{/* federation-listening-ip inherits listening-ip, but only matters when
     federation is enabled. */}}
{{- if .Values.federate.enabled -}}
{{- $vals = append $vals (include "coturn.federationListenIp" .) -}}
{{- end -}}
{{- $found := false -}}
{{- range $v := $vals -}}
{{- if contains $ph (toString $v) -}}
{{- $found = true -}}
{{- end -}}
{{- end -}}
{{- if $found -}}true{{- end -}}
{{- end -}}
