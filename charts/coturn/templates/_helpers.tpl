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
i.e. depends on the node's discovered external IP. This is the case when
listening-ip or relay-ip fall back to the placeholder (their unset default), or
when external-ip is explicitly pinned to it. Returns "true" or "".
*/}}
{{- define "coturn.usesExternalIpPlaceholder" -}}
{{- $ph := "__COTURN_EXT_IP__" -}}
{{- $listen := default $ph .Values.coturnTurnListenIP -}}
{{- $relay := default $ph .Values.coturnTurnRelayIP -}}
{{- if or (eq $listen $ph) (eq $relay $ph) (and .Values.coturnTurnExternalIP (eq .Values.coturnTurnExternalIP $ph)) -}}
true
{{- end -}}
{{- end -}}

{{/*
Resolve whether the get-external-ip helper should run. externalIpHelper.enabled
is tri-state: null (default) auto-detects from whether __COTURN_EXT_IP__ is
actually used; an explicit true/false overrides the auto-detection.
Returns "true" or "".
*/}}
{{- define "coturn.externalIpHelperEnabled" -}}
{{- $e := .Values.externalIpHelper.enabled -}}
{{- if kindIs "invalid" $e -}}
{{- include "coturn.usesExternalIpPlaceholder" . -}}
{{- else if $e -}}
true
{{- end -}}
{{- end -}}
