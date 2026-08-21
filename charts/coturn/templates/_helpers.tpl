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
{{- $vals := list (default $ph .Values.coturnTurnListenIP) (default $ph .Values.coturnTurnRelayIP) -}}
{{/* external-ip only renders when set; it may embed the placeholder in the
     public/private form, e.g. __COTURN_EXT_IP__/__COTURN_HOST_IP__. */}}
{{- if .Values.coturnTurnExternalIP -}}
{{- $vals = append $vals .Values.coturnTurnExternalIP -}}
{{- end -}}
{{/* federation-listening-ip also falls back to the placeholder, but only when
     federation is enabled. */}}
{{- if .Values.federate.enabled -}}
{{- $vals = append $vals (default $ph .Values.coturnFederationListeningIP) -}}
{{- end -}}
{{- $found := false -}}
{{- range $v := $vals -}}
{{- if contains $ph (toString $v) -}}
{{- $found = true -}}
{{- end -}}
{{- end -}}
{{- if $found -}}true{{- end -}}
{{- end -}}
