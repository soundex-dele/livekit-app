import {Alert, Button, Card, Descriptions, Space, Table, Typography} from 'antd';
import {useQuery} from '@tanstack/react-query';
import {useAdminScope} from './api';
import {emptyTable, PageHeading, RequestError, StateTag} from './components';
import {formatTime} from './domain';
import type {MediaNode} from './types';

export function NodesPage() {
  const scope = useAdminScope();
  const nodes = useQuery({queryKey: [...scope.key, 'nodes'], enabled: !!scope.actor?.user.admin && !scope.organization, queryFn: ({signal}) => scope.client.get<{items: MediaNode[]}>('/av/admin/platform/media-nodes', {signal}), refetchInterval: 10000, refetchIntervalInBackground: false});
  const columns = [{title: '节点', dataIndex: 'id'}, {title: '健康状态', dataIndex: 'status', render: (state: string) => <StateTag state={state}/>}, {title: '房间绑定 / 容量', key: 'capacity', render: (_: unknown, row: MediaNode) => `${row.bindings} / ${row.capacity}`}, {title: '配置状态', key: 'config', render: (_: unknown, row: MediaNode) => !row.configured ? '已移出配置' : row.enabled ? '已启用' : '未启用'}, {title: '最近检查', dataIndex: 'updated', render: formatTime}, {title: '启动代次', dataIndex: 'incarnation', ellipsis: true}];
  return <div className="av-page"><PageHeading title="媒体节点" description="只读查看节点健康与房间分配情况。配置变更通过部署流程维护。" extra={<Button onClick={() => void nodes.refetch()} loading={nodes.isFetching}>刷新节点</Button>}/><Alert type="info" showIcon message="节点状态每 10 秒刷新" description="检查时间过期时显示为未知或过期；配置容量表示房间分配上限。"/>{nodes.error && <RequestError error={nodes.error} retry={() => void nodes.refetch()}/>}
    <Card className="av-desktop-table"><Table rowKey="id" locale={emptyTable} loading={nodes.isLoading} dataSource={nodes.data?.items ?? []} columns={columns} pagination={false} scroll={{x: 850}}/></Card>
    <div className="av-mobile-cards">{nodes.data?.items.map(row => <Card key={row.id} title={row.id} extra={<StateTag state={row.status}/>}><Descriptions column={1} items={[{key: 'load', label: '绑定 / 容量', children: `${row.bindings} / ${row.capacity}`}, {key: 'time', label: '最近检查', children: formatTime(row.updated)}]}/><Space><Typography.Text type="secondary">{row.configured ? '配置内节点' : '已移出配置'}</Typography.Text></Space></Card>)}</div>
  </div>;
}
