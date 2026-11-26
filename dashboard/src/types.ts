// These mirror the Java records in control-plane/.../Dtos.java.
export type Decision = 'ALLOW' | 'BLOCK';

export interface Rule {
  id: number;
  pattern: string;
  action: Decision;
  category: string;
  reason: string;
  enabled: boolean;
  createdAt: string;
  updatedAt: string;
}

export interface RuleInput {
  pattern: string;
  action: Decision;
  category: string;
  reason: string;
  enabled: boolean;
}

export interface Settings {
  defaultAction: Decision;
  policyVersion: number;
}

export interface Summary {
  total: number;
  allowed: number;
  blocked: number;
  avgDecisionUs: number;
  policyVersion: number;
  defaultAction: Decision;
  activeRules: number;
}

export interface Bucket {
  minute: string;
  allowed: number;
  blocked: number;
}

export interface HostCount {
  host: string;
  count: number;
}

export interface LogRow {
  id: number;
  ts: string;
  clientIp: string | null;
  host: string;
  port: number;
  method: string;
  decision: Decision;
  ruleId: number | null;
  rulePattern: string | null;
  category: string | null;
  reason: string | null;
  policyVersion: number;
  decisionUs: number;
  bytesUp: number;
  bytesDown: number;
  durationMs: number;
}
