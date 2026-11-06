import type {components} from './generated-contract';

type Schemas = components['schemas'];
export type Business = 'conference' | 'screen-cast' | 'remote-control' | 'live-studio';
export type Page<T> = Omit<Schemas['AVAdminResourcePage'], 'items'> & {items: T[]};
export type Resource = Schemas['AVAdminResource'];
export type Member = Schemas['AVAdminDetail']['members'][number];
export type Binding = Schemas['AVAdminDetail']['bindings'][number];
export type GovernanceRecord = Schemas['AVAdminDetail']['actions'][number];
// The backend deliberately exposes business-specific detail as an open map.
// All shared envelopes and members come from the combined contract.
export type ResourceDetail = Omit<Schemas['AVAdminDetail'], 'details'> & {details: {
  meeting?: {number: string; host: string; starts_at: number; duration_minutes: number; waiting_room: boolean; locked: boolean; sharing_member: string};
  remote?: {host: string; controller: string; epoch: number; control_grants: {id: string; state: string; epoch: number; expires: number; created: number}[]};
  cast?: {sender: string; receiver: string};
  live?: {room_id: string; host: string; pk_id: string; started: number};
}};
export type MediaOperation = Schemas['AVAdminOperation'];
export type Overview = Schemas['AVAdminOverview'];
export type MediaNode = Schemas['AVAdminNode'];
export type ActionInput = Schemas['AVAdminAction'];
export type GovernanceAction = ActionInput['action'];
export type ActionResult = Schemas['AVAdminActionResult'];
export interface AVCapabilities {modules: string[]; organization_required: boolean; api_version: string; admin_url?: string}
