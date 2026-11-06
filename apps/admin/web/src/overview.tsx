import {Alert, Button, Card, Col, Row, Skeleton, Space, Statistic, Table, Typography} from 'antd';
import {useQuery} from '@tanstack/react-query';
import {useAdminScope} from './api';
import {businessNames, formatTime, pendingOperation, terminalResource} from './domain';
import {emptyTable, NoOrganization, PageHeading, RequestError, StateTag} from './components';
import type {Business, Overview} from './types';

export function OverviewPage() {
  const scope = useAdminScope();
  const data = useQuery({queryKey: [...scope.key, 'overview'], enabled: scope.allowed, queryFn: ({signal}) => scope.client.get<Overview>(scope.path + '/overview', {signal}), refetchInterval: 10000, refetchIntervalInBackground: false});
  if (!scope.allowed) return <NoOrganization/>;
  const value = data.data;
  const total = value?.resources.reduce((sum, row) => sum + row.count, 0) ?? 0;
  const active = value?.resources.filter(row => !terminalResource(row.state)).reduce((sum, row) => sum + row.count, 0) ?? 0;
  const pending = value?.operations.filter(row => pendingOperation(row.state)).reduce((sum, row) => sum + row.count, 0) ?? 0;
  const failed = value?.operations.filter(row => row.state === 'failed').reduce((sum, row) => sum + row.count, 0) ?? 0;
  return <div className="av-page"><PageHeading title="运营概览" description={scope.organization ? '掌握当前组织的业务运行与治理进度。' : '查看全平台业务概况，选择组织后进入具体管理。'} extra={<Button onClick={() => void data.refetch()} loading={data.isFetching}>刷新概览</Button>}/>
    {data.error && <RequestError error={data.error} retry={() => void data.refetch()}/>}
    {data.isLoading ? <Skeleton active/> : <>
      <Row gutter={[16, 16]}>{[['资源总数', total], ['未结束资源', active], ['近 24 小时新增', value?.created_last_24h ?? 0], ['待处理媒体操作', pending]].map(([title, count]) => <Col xs={24} sm={12} xl={6} key={title}><Card><Statistic title={title} value={count}/></Card></Col>)}</Row>
      {failed > 0 && <Alert type="warning" showIcon message={`有 ${failed} 项媒体操作失败`} description="业务状态可能已经更新，但媒体清理尚未完成。请检查操作详情和关联任务。" action={<Button onClick={() => {location.hash = 'av-operations';}}>查看媒体操作</Button>}/>}
      <Row gutter={[16, 16]}>{(Object.entries(businessNames) as [Business, string][]).map(([business, name]) => <Col xs={24} lg={12} key={business}><Card title={`${name}状态`} extra={<Button type="link" onClick={() => {location.hash = 'av-' + business;}}>查看{name}</Button>}><Space wrap>{value?.resources.filter(row => row.business === business).map(row => <span className="av-count" key={row.state}><StateTag state={row.state}/><strong>{row.count}</strong></span>)}{!value?.resources.some(row => row.business === business) && <Typography.Text type="secondary">暂无{name}记录</Typography.Text>}</Space></Card></Col>)}</Row>
      <Card title="媒体操作状态"><Table locale={emptyTable} rowKey="state" size="small" pagination={false} dataSource={value?.operations ?? []} columns={[{title: '状态', dataIndex: 'state', render: (state: string) => <StateTag state={state}/>}, {title: '数量', dataIndex: 'count'}]}/></Card>
      <Typography.Text type="secondary">数据更新于 {formatTime(value?.generated_at)} · 每 10 秒刷新。资源和成员状态来自业务记录。</Typography.Text>
    </>}
  </div>;
}
