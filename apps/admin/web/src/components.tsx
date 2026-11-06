import {Alert, Button, Empty, Space, Tag, Typography} from 'antd';
import type {ReactNode} from 'react';
import {errorText, stateNames} from './domain';

export function StateTag({state}: {state: string}) {
  const color = ['failed', 'unhealthy'].includes(state) ? 'error' : ['healthy', 'succeeded', 'active', 'live'].includes(state) ? 'success' : ['queued', 'retrying', 'pending', 'waiting', 'stale'].includes(state) ? 'warning' : 'default';
  return <Tag color={color}>{stateNames[state] || state}</Tag>;
}
export function RequestError({error, retry}: {error: unknown; retry?: () => void}) {
  return <Alert type="error" showIcon message="操作未完成" description={errorText(error)} action={retry && <Button onClick={retry}>重试</Button>}/>;
}
export function PageHeading({title, description, extra}: {title: string; description: string; extra?: ReactNode}) {
  return <div className="av-page-heading"><div><Typography.Text className="av-eyebrow">AV PLATFORM</Typography.Text><Typography.Title level={1}>{title}</Typography.Title><Typography.Paragraph type="secondary">{description}</Typography.Paragraph></div><Space wrap>{extra}</Space></div>;
}
export function NoOrganization() {return <Empty description="请在上方选择一个有管理权限的组织。"/>;}
export const emptyTable = {emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="当前条件下暂无记录"/>};
