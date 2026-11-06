import {AdminApp, type AdminPage} from '@platform/admin';
import {useSession} from '@platform/react';
import {useEffect, useState} from 'react';
import {Alert} from 'antd';
import {OverviewPage} from './overview';
import {ResourcesPage} from './resources';
import {OperationsPage} from './operations';
import {NodesPage} from './nodes';
import {businessNames} from './domain';
import type {Business} from './types';

const builtinPages = ['users', 'sessions', 'api-keys', 'organizations', 'members', 'roles', 'audit', 'jobs', 'events', 'operations', 'settings'];
export function AdminApplication() {
  const [reducedMotion, setReducedMotion] = useState(() => window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  useEffect(() => {
    const preference = window.matchMedia('(prefers-reduced-motion: reduce)');
    const update = () => setReducedMotion(preference.matches);
    update();
    preference.addEventListener('change', update);
    return () => preference.removeEventListener('change', update);
  }, []);
  const {actor, organization, management} = useSession();
  const allowed = actor && (actor.user.admin || organization && (actor.permissions.includes('*') || actor.permissions.includes('av.manage')));
  const scope = `${management}:${organization}`;
  const pages: AdminPage[] = allowed ? [
    {key: 'av-overview', name: '运营概览', group: '运营', content: <OverviewPage key={scope}/>},
    ...(Object.entries(businessNames) as [Business, string][]).map(([business, name]) => ({key: `av-${business}`, name: `${name}管理`, group: '音视频业务', content: <ResourcesPage key={`${scope}:${business}`} business={business}/>})),
    {key: 'av-operations', name: '媒体操作', group: '运维', content: <OperationsPage key={scope}/>},
    ...(actor?.user.admin && !organization ? [{key: 'av-nodes', name: '媒体节点', group: '运维', content: <NodesPage key={scope}/>}]: []),
  ] : actor ? [{key: 'av-welcome', name: '管理范围', group: '运营', content: <Alert type="info" showIcon message="请选择组织" description="选择拥有管理权限的组织后，即可进入业务运营页面。"/>}] : [];
  return <AdminApp title="AV 运营管理" management primary="#2563EB" tokens={{fontFamily: 'Segoe UI, Microsoft YaHei UI, PingFang SC, system-ui, sans-serif', borderRadius: 8, motion: !reducedMotion}} defaultPage={allowed ? 'av-overview' : 'av-welcome'} builtinPages={builtinPages} extraPages={pages}/>;
}
