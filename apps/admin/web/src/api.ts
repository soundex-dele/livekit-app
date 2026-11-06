import {useFoundation, useSession} from '@platform/react';
import {useQuery} from '@tanstack/react-query';
import {scopeKey, scopePath} from './domain';
import type {AVCapabilities} from './types';

export function useAdminScope() {
  const {client} = useFoundation();
  const {actor, organization, management} = useSession();
  const allowed = !!actor && (actor.user.admin || !!organization && (actor.permissions.includes('*') || actor.permissions.includes('av.manage')));
  return {client, actor, organization, management, allowed, path: scopePath(organization), key: scopeKey(organization, management), identity: `${management}:${organization}`};
}
export function useAVCapabilities() {
  const {client} = useFoundation();
  const {actor} = useSession();
  return useQuery({queryKey: ['av-capabilities'], enabled: !!actor, queryFn: ({signal}) => client.get<AVCapabilities>('/av/capabilities', {signal}), staleTime: 30000});
}
